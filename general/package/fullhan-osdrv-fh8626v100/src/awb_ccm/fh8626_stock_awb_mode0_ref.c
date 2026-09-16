#include "fh8626_stock_awb_mode0_ref.h"
#include <stddef.h>
#include <errno.h>
#include <string.h>

static const uint8_t bayer_perm[4][4] = {
    {0,1,3,2}, {1,0,2,3}, {3,2,0,1}, {2,3,1,0}
};
static int16_t signed_low16(int32_t value);
static int32_t signed_word(uint32_t v)
{return v<=0x7fffffffu?(int32_t)v:-1-(int32_t)~v;}

int fh_stock_awb_normalize_targets(const int16_t base[3],uint32_t isp_4d8,
                                   uint32_t isp_4dc,int8_t ctx_dc,
                                   uint32_t *isp_4bc,int16_t out_gain[3])
{
    uint32_t corr;
    int32_t total,mn,ctrl;
    int16_t out[3];
    unsigned i;
    if(!base||!isp_4bc||!out_gain)return -EINVAL;
    if(!base[0]||!base[1]||!base[2])return -EDOM;
    /* CA41C and CAF24 are both 001FFE00, NOT 00001FFF.
       27602C divides unsigned words, including sign-extended base arguments. */
    corr=((isp_4d8>>7)&0x1ffe00u)/(uint32_t)(int32_t)base[1];
    corr+=((isp_4d8<<9)&0x1ffe00u)/(uint32_t)(int32_t)base[0];
    corr+=((isp_4dc<<9)&0x1ffe00u)/(uint32_t)(int32_t)base[2];
    total=signed_low16((int32_t)(corr&0xffffu));
    for(i=0;i<3;i++)out[i]=(int16_t)(signed_word(
        ((uint32_t)total*(uint32_t)(int32_t)base[i])<<6)>>16);
    mn=out[0]<out[1]?out[0]:out[1];if(out[2]<mn)mn=out[2];
    ctrl=(((mn*1023)>>9)*((int32_t)ctx_dc+128))>>7;
    if(ctrl>1023)ctrl=1023;
    if(ctrl<0)ctrl=0;
    *isp_4bc=(*isp_4bc&0xffff0000u)|(uint32_t)ctrl;
    memcpy(out_gain,out,sizeof(out));
    return 0;
}

int fh_stock_ca13c(const struct fh_stock_awb_stat_rec rec[9],
                          uint32_t isp_4d8, uint32_t isp_4dc,
                          int8_t ctx_dc, uint32_t *isp_4bc,
                          uint32_t *ctx_ac_word,const int16_t last_good[3],
                          int16_t out_gain[3])
{
    uint64_t s0=0,s1=0,s2=0;
    uint32_t cnt=0,i,c0,c1,c2,word;
    int32_t m0=0,m1=0,m2=0,mn;
    int16_t gains[3];
    int fallback=0,rc;
    if(!rec||!isp_4bc||!ctx_ac_word||!out_gain)return -1;
    for(i=0;i<9;i++){s0+=rec[i].c0;s1+=rec[i].c1;s2+=rec[i].c2;cnt+=rec[i].count;}
    if(cnt){m0=signed_word((uint32_t)(s0/cnt));m1=signed_word((uint32_t)(s1/cnt));m2=signed_word((uint32_t)(s2/cnt));}
    mn=m0<m1?m0:m1;if(m2<mn)mn=m2;
    if(!cnt||!mn){
        if(!last_good)return 1;
        memcpy(gains,last_good,sizeof(gains));fallback=1;
    }else{
        uint64_t num=(uint64_t)(uint32_t)mn<<9;
        gains[0]=signed_low16((int32_t)((num/(uint32_t)(m0<1?1:m0))&0xffffu));
        gains[1]=signed_low16((int32_t)((num/(uint32_t)(m1<1?1:m1))&0xffffu));
        gains[2]=signed_low16((int32_t)((num/(uint32_t)(m2<1?1:m2))&0xffffu));
    }
    c0=((uint32_t)(m0>>2))&0x3ffu;
    c1=((uint32_t)(m1>>2))&0x3ffu;
    c2=((uint32_t)(m2>>2))&0x3ffu;

    rc=fh_stock_awb_normalize_targets(gains,isp_4d8,isp_4dc,ctx_dc,isp_4bc,out_gain);
    if(rc)return rc;

    word=*ctx_ac_word;
    word=(word&~0x000003ffu)|c0;
    word=(word&~0x000ffc00u)|(c1<<10);
    {
        uint16_t ae=(uint16_t)(word>>16);
        ae=(uint16_t)((ae&0xc00fu)|((c2&0x3ffu)<<4));
        word=(word&0x0000ffffu)|((uint32_t)ae<<16);
    }
    *ctx_ac_word=word;
    return fallback;
}

int fh_stock_ca13c_normal(const struct fh_stock_awb_stat_rec rec[9],
                          uint32_t isp_4d8,uint32_t isp_4dc,int8_t ctx_dc,
                          uint32_t *isp_4bc,uint32_t *ctx_ac_word,int16_t out_gain[3])
{return fh_stock_ca13c(rec,isp_4d8,isp_4dc,ctx_dc,isp_4bc,ctx_ac_word,NULL,out_gain);}

/* ARM LSL16/ASR16 and STRH, without implementation-defined narrowing. */
static int16_t signed_low16(int32_t value)
{
    uint32_t low=(uint32_t)value&0xffffu;
    return (int16_t)(low<0x8000u?(int32_t)low:(int32_t)low-65536);
}

static int16_t stock_step(int16_t cur,int16_t target,uint8_t speed,uint8_t flags)
{
    int32_t d=signed_low16((int32_t)target-cur);
    int32_t ad=d<0?-d:d;
    int32_t step=ad>30?2*((speed&15)+1):1;
    if(!d)return cur;
    if(flags&1u)return target;
    return signed_low16((int32_t)cur+(d<0?-1:1)*step);
}

void fh_stock_cafc0(struct fh_stock_awb_commit_state *st,
                     const int16_t target[3],uint8_t *ctx,
                     uint32_t isp_024,uint32_t isp_084,uint32_t isp_088,
                     fh_stock_awb_sensor_gain_fn sensor_gain,void *opaque,
                     uint32_t *isp_224,uint32_t *isp_228)
{
    uint32_t sum=0,hi,avg,scale,logical[4],phys[4];
    int32_t diff[3];
    unsigned i,row;
    int hold=0;
    uint8_t flags,speed;
    if(!st||!target||!ctx||!isp_224||!isp_228)return;
    diff[0]=signed_low16((int32_t)target[0]-st->cur0);
    diff[1]=signed_low16((int32_t)target[1]-st->cur1);
    diff[2]=signed_low16((int32_t)target[2]-st->cur2);
    for(i=0;i<3;i++)sum+=(uint32_t)(diff[i]<0?-diff[i]:diff[i]);
    speed=ctx[0x6d];flags=ctx[0x6e];hi=speed>>4;
    if(st->mode==1u){
        if(sum<(ctx[0x6c]>>2)){
            st->ctr0=st->ctr1=st->ctr2=0;hold=1;
        }else if(sum<70u&&st->ctr0<(hi<<6)){
            st->ctr0++;st->ctr1=st->ctr2=0;hold=1;
        }else if(sum>=70u&&sum<150u&&st->ctr0<(hi<<6)&&st->ctr1<(hi<<5)){
            st->ctr0++;st->ctr1++;st->ctr2=0;hold=1;
        }else if(sum>=150u&&st->ctr0<(hi<<6)&&st->ctr1<(hi<<5)&&st->ctr2<(hi<<4)){
            st->ctr0++;st->ctr1++;st->ctr2++;hold=1;
        }
    }
    if(!hold){
        st->ctr0=st->ctr1=st->ctr2=st->mode=0;
        st->cur2=stock_step(st->cur2,target[2],speed,flags);
        st->cur0=stock_step(st->cur0,target[0],speed,flags);
        st->cur1=stock_step(st->cur1,target[1],speed,flags);
        if(sum<=3u)st->mode=1;
    }
    /* Current Ghidra CB4E4=FFF: average<=4095, denominator cannot be zero.
       Inputs are a caller-supplied BLC snapshot, not an atomic-MMIO claim. */
    avg=((isp_084&0xfffu)+((isp_084>>16)&0xfffu)+
         (isp_088&0xfffu)+((isp_088>>16)&0xfffu))>>2;
    scale=0x100000u/(4096u-avg);
    logical[0]=((uint32_t)(int32_t)st->cur0*scale)>>8;
    logical[1]=((uint32_t)(int32_t)st->cur1*scale)>>8;
    logical[2]=((uint32_t)(int32_t)st->cur2*scale)>>8;
    if(flags&2u){
        /* CB4E8=3FF. These persistent words model AWB module E0/E4/E8,
           not the logical cur[] state. Callback return is ignored by stock. */
        for(i=0;i<3;i++){
            st->sensor_gain[i]=logical[i]>1023u?1023u:logical[i];
            logical[i]=logical[i]>1023u?(logical[i]>>10)<<9:512u;
        }
        if(sensor_gain)sensor_gain(opaque,st->sensor_gain);
        flags=ctx[0x6e]; /* CB248 and callback-absent CB4D0 both reload. */
    }
    if(flags&16u)for(i=0;i<3;i++)logical[i]=(logical[i]*ctx[0x7c+i])>>6;
    logical[3]=logical[1];row=(isp_024>>1)&3u;
    for(i=0;i<4;i++)phys[bayer_perm[row][i]]=logical[i];
    /* CB2CC/CB2D0 OR the whole low word; do not silently clamp/mask it. */
    *isp_224=phys[0]|(phys[1]<<16);
    *isp_228=phys[2]|(phys[3]<<16);
}

void fh_stock_cafc0_day(struct fh_stock_awb_commit_state *st,
                        const int16_t target[3],uint32_t isp_024,
                        uint32_t isp_084,uint32_t isp_088,
                        uint32_t *isp_224,uint32_t *isp_228)
{
    uint8_t ctx[0x7f]={0};
    ctx[0x6c]=1;ctx[0x6d]=0x0f;
    fh_stock_cafc0(st,target,ctx,isp_024,isp_084,isp_088,
                  NULL,NULL,isp_224,isp_228);
}
