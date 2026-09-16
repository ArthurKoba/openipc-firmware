#include "fh8626_stock_ca4f4_diag.h"
#include "../awb_ccm/fh8626_stock_awb_mode0_ref.h"
#include <errno.h>
#include <limits.h>
#include <string.h>

static uint32_t r32(const uint8_t *p){uint32_t v;memcpy(&v,p,4);return v;}
static uint32_t udiv32(uint32_t a,uint32_t b){return b?a/b:0xffffffffu;}
static uint32_t q12(uint32_t a,uint32_t b){return b?(uint32_t)(((uint64_t)a<<12)/b):0xffffffffu;}

static unsigned ca4f4_weight(const uint8_t *ctx,uint32_t spread,uint32_t min_ratio)
{
    uint32_t t75=(uint32_t)ctx[0x75]<<7;
    uint32_t t76=(uint32_t)ctx[0x76]<<7;
    uint32_t t77=(uint32_t)ctx[0x77]<<7;
    uint32_t d=t77-t76;
    uint32_t t;
    t=t76+(3u*d)/4u; if(spread>=t && min_ratio<=0x0ce3u)return 8;
    t=t76+d/2u;      if(spread>=t && min_ratio<=0x0e65u)return 7;
    t=t76+d/4u;      if(spread>=t)return 6;
    if(spread>=t76)return 4;
    {
        d=t76-t75;
        t=t75+(2u*d)/3u; if(spread>=t)return 3;
        t=t75+d/3u;      if(spread>=t)return 2;
    }
    if(spread>=t75)return 1;
    return 0;
}

static unsigned stock_median_slot(uint32_t v[9],unsigned valid_count)
{
    unsigned idx[9],i,j,offset=9u-valid_count,k;
    for(i=0;i<9;i++)idx[i]=i;
    for(i=0;i<8;i++)for(j=i+1;j<9;j++)if(v[j]<v[i]){
        uint32_t tv=v[i];unsigned ti=idx[i];v[i]=v[j];idx[i]=idx[j];v[j]=tv;idx[j]=ti;
    }
    k=(9u+offset)/2u;
    if(k>8)k=8;
    return idx[k];
}

static int make_gain3(uint64_t s0,uint64_t s1,uint64_t s2,int16_t g[3])
{
    uint64_t mn=s0<s1?s0:s1;mn=mn<s2?mn:s2;
    if(!s0||!s1||!s2)return -1;
    g[0]=(int16_t)((mn<<9)/s0);
    g[1]=(int16_t)((mn<<9)/s1);
    g[2]=(int16_t)((mn<<9)/s2);
    return 0;
}

static int final_normalize(struct fh_stock_ca4f4_diag *d,const uint8_t *ctx,const volatile uint32_t *regs)
{
    return fh_stock_awb_normalize_targets(d->mixed,regs[0x4d8/4],
        regs[0x4dc/4],(int8_t)ctx[0xdc],&d->reg4bc_next,d->final_gain);
}

static uint32_t publish_channel_means(uint32_t word,uint64_t s0,uint64_t s1,uint64_t s2,uint32_t count)
{
    uint32_t c0,c1,c2;
    uint16_t hi;
    if(!count)return word&0xc0000000u;
    /* Stock CA4F4 first converts each accumulated channel to the quarter-scale
       domain, then divides by the valid-cell population before 10-bit packing. */
    c0=(uint32_t)((s0>>2)/count)&0x3ffu;
    c1=(uint32_t)((s1>>2)/count)&0x3ffu;
    c2=(uint32_t)((s2>>2)/count)&0x3ffu;
    word=(word&~0x000003ffu)|c0;
    word=(word&~0x000ffc00u)|(c1<<10);
    hi=(uint16_t)(word>>16);
    hi=(uint16_t)((hi&0xc00fu)|(c2<<4));
    return (word&0xffffu)|((uint32_t)hi<<16);
}

int fh_stock_ca4f4_compute(struct fh_stock_ca4f4_diag *d,
                                const uint8_t *ctx,const volatile uint32_t *regs,
                                const uint8_t *stats_src,
                                const int16_t last_good_gain[3],
                                struct fh_stock_ca4f4_state *state,
                                fh_stock_ca4f4_profile_fn profile,void *opaque)
{
    uint64_t s0=0,s1=0,s2=0;
    uint32_t vals[9];
    unsigned i;
    int recovering=0,rc;
    if(!d||!ctx||!regs||!stats_src||!last_good_gain||!state)return -EINVAL;
    memset(d,0,sizeof(*d));d->ratio_min=0x10000u;d->ctx_ac_next=r32(ctx+0xac);
    d->reg4bc_next=regs[0x4bc/4];d->profile_selector=-1;
    /* CA4F4 reads CAFC0's persistent logical gains at +0x20/+0x22/+0x24.
       ISP +0x224/+0x228 contain a later black-level-scaled Bayer permutation
       and therefore are not the stock fallback state. */
    d->fallback[0]=(uint16_t)last_good_gain[0];
    d->fallback[1]=(uint16_t)last_good_gain[1];
    d->fallback[2]=(uint16_t)last_good_gain[2];
    /* stats_src contains CB7B0-normalized nine records. Raw C67C4(0)
       bytes are equivalent only when all CB580 references are512. */
    for(i=0;i<9;i++){
        const uint8_t *p=stats_src+i*16u;
        uint32_t c0=r32(p),c1=r32(p+4),c2=r32(p+8),cnt=r32(p+12),q02=0,q21,q01;
        d->raw[i][0]=c0;d->raw[i][1]=c1;d->raw[i][2]=c2;d->raw[i][3]=cnt;
        /* CA4F4 increments the quality population for every cell before
           deciding whether that cell has usable channel ratios. */
        d->count_sum+=cnt;
        if(cnt<=10||c2==0||c1==0){vals[i]=0;continue;}
        q02=q12(c0,c2);if(q02==0xffffffffu){vals[i]=0;continue;}
        q21=q12(c2,c1);q01=q12(c0,c1);
        if(q21==UINT_MAX||q01==UINT_MAX){vals[i]=0;continue;}
        d->valid[i]=1;d->valid_count++;d->valid_count_sum+=cnt;d->ratio_c0_c2[i]=q02;vals[i]=q02;
        if(q02<d->ratio_min)d->ratio_min=q02;
        if(q02>d->ratio_max)d->ratio_max=q02;
        d->ratio_sum_02+=q02;d->ratio_sum_21+=q21;d->ratio_sum_01+=q01;
        s0+=c0;s1+=c1;s2+=c2;
    }
    if(!d->valid_count||d->count_sum<=89){
        state->recovery_active=1;d->profile_selector=1;
        if(profile)profile(opaque,1);
        d->stats_fallback=1;memcpy(d->final_gain,d->fallback,6);return 1;
    }
    if(!state->recovery_active){
        d->profile_selector=1;if(profile)profile(opaque,1);
        state->recovery_active=0;
    }else if(++state->recovery_count>20u){
        state->recovery_count=state->recovery_active=0;
    }else recovering=1;
    if(recovering&&!(ctx[0x71]&2u)){
        uint32_t hi=(uint32_t)ctx[0x74]<<8;
        uint32_t lo;
        if(d->ratio_sum_02<hi&&!ctx[0x74])return -EDOM;
        lo=udiv32(65536u,ctx[0x74]);
        if(d->ratio_sum_02>=hi || d->ratio_sum_02<=lo ||
           (d->ratio_sum_01<=lo && d->ratio_sum_21<=lo)){
            d->gate_fallback=1;memcpy(d->final_gain,d->fallback,6);return 2;
        }
    }
    if(make_gain3(s0,s1,s2,d->base))return -EDOM; /* stock asserts */
    memcpy(d->mixed,d->base,sizeof(d->mixed));
    if(recovering)goto normalize; /* CA8A8 skips robust median/blending. */
    d->weight=ca4f4_weight(ctx,d->ratio_max-d->ratio_min,d->ratio_min);
    if(d->valid_count>=3){
        unsigned m=stock_median_slot(vals,d->valid_count);
        uint32_t c0=d->raw[m][0],c1=d->raw[m][1],c2=d->raw[m][2];
        uint32_t mn=c0<c1?c0:c1;mn=mn<c2?mn:c2;
        d->median_slot=m;
        if(c0&&c1&&c2){
            d->robust[0]=(int16_t)(((uint64_t)mn<<9)/c0);
            d->robust[1]=(int16_t)(((uint64_t)mn<<9)/c1);
            d->robust[2]=(int16_t)(((uint64_t)mn<<9)/c2);
            for(i=0;i<3;i++)d->mixed[i]=(int16_t)(((8u-d->weight)*(int32_t)d->base[i]+d->weight*(int32_t)d->robust[i])/8);
        }
    } else d->weight=0;
normalize:
    rc=final_normalize(d,ctx,regs);if(rc)return rc;
    d->ctx_ac_next=publish_channel_means(d->ctx_ac_next,s0,s1,s2,d->valid_count_sum);
    d->publish_estimator=1;
    return 0;
}

int fh_stock_ca4f4_diag_compute(struct fh_stock_ca4f4_diag *d,
                                const uint8_t *ctx,const volatile uint32_t *regs,
                                const uint8_t *stats,const int16_t last_good[3])
{
    struct fh_stock_ca4f4_state state={0,0};
    return fh_stock_ca4f4_compute(d,ctx,regs,stats,last_good,&state,NULL,NULL);
}
