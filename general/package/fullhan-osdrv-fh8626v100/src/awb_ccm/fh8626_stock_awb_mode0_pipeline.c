#include "fh8626_stock_awb_mode0_pipeline.h"
#include "fh8626_stock_c9f68_ref.h"
#include <string.h>
#include <errno.h>

static uint32_t r32(const uint8_t *p){uint32_t v;memcpy(&v,p,4);return v;}
static void w32(uint8_t *p,uint32_t v){memcpy(p,&v,4);}
static void w16(uint8_t *p,uint16_t v){memcpy(p,&v,2);}
static int32_t sdiv32(int32_t a,int32_t b){return b?a/b:0;}

static const unsigned snapshot_offsets[16]={0x224,0x228,0x4bc,0x5d4,
    0x4c0,0x4c4,0x4c8,0x4cc,0x4d0,0x4d4,0x18c,0x190,0x194,0x198,0x19c,0x1a0};
void fh_stock_awb_snapshot_save(struct fh_stock_awb_snapshot *snapshot,
                                 const struct fh_stock_awb_mode0 *state,
                                 const uint8_t *ctx,const volatile uint32_t *regs,
                                 uint32_t profile,uint64_t generation)
{
    unsigned i;
    snapshot->state=*state;
    memcpy(snapshot->context_a4_b3,ctx+0xa4,16);
    memcpy(snapshot->context_ccm,ctx+0x140,24);
    snapshot->gate_bit=ctx[0x10]&2u;snapshot->mode_bits=ctx[0x6c]&3u;
    for(i=0;i<16;i++)snapshot->registers[i]=regs[snapshot_offsets[i]/4];
    snapshot->profile_generation=profile;snapshot->stream_generation=generation;
    snapshot->valid=1;
}
int fh_stock_awb_snapshot_restore(struct fh_stock_awb_snapshot *snapshot,
                                   struct fh_stock_awb_mode0 *state,uint8_t *ctx,
                                   volatile uint32_t *regs,uint32_t profile,uint64_t generation)
{
    unsigned i;
    uint32_t epoch;
    if(!snapshot||!state||!ctx||!regs)return -EINVAL;
    if(!snapshot->valid)return -ENOENT;
    if(snapshot->profile_generation!=profile||snapshot->stream_generation!=generation)
        return -ESTALE;
    for(i=0;i<16;i++)regs[snapshot_offsets[i]/4]=snapshot->registers[i];
    memcpy(ctx+0xa4,snapshot->context_a4_b3,16);
    memcpy(ctx+0x140,snapshot->context_ccm,24);
    ctx[0x10]=(ctx[0x10]&(uint8_t)~2u)|snapshot->gate_bit;
    ctx[0x6c]=(ctx[0x6c]&(uint8_t)~3u)|snapshot->mode_bits;
    epoch=state->dispatch_epoch;
    *state=snapshot->state;state->dispatch_epoch=epoch;snapshot->valid=0;
    return 0;
}

void fh_stock_awb_init_triplet(struct fh_stock_awb_mode0 *s,const uint32_t triplet[3])
{
    /* STRH preserves the low16 bits, including values >=0x8000. This is not
     * inverse black-level compensation and must not memset the module. */
    uint16_t v;
    v=(uint16_t)triplet[0];memcpy(&s->commit.cur0,&v,sizeof(v));
    v=(uint16_t)triplet[1];memcpy(&s->commit.cur1,&v,sizeof(v));
    v=(uint16_t)triplet[2];memcpy(&s->commit.cur2,&v,sizeof(v));
    s->commit_valid=1;
}

void fh_stock_awb_commit_seed_bayer(struct fh_stock_awb_commit_state *s,
                                    uint32_t isp024,uint32_t isp084,uint32_t isp088,
                                    uint32_t r224,uint32_t r228)
{
    uint32_t phys[4]={r224&0xffffu,r224>>16,r228&0xffffu,r228>>16};
    static const uint8_t perm[4][4]={{0,1,3,2},{1,0,2,3},{3,2,0,1},{2,3,1,0}};
    unsigned row=(isp024>>1)&3u;
    uint32_t avg=((isp084&0xfffu)+((isp084>>16)&0xfffu)+
                  (isp088&0xfffu)+((isp088>>16)&0xfffu))>>2;
    uint32_t scale=0x100000u/(4096u-avg),logical[3];
    unsigned i;
    for(i=0;i<3;i++){
        logical[i]=(phys[perm[row][i]]*256u+scale/2u)/scale;
        if(logical[i]>32767u)logical[i]=32767u;
    }
    memset(s,0,sizeof(*s));
    /* logical {cur0,cur1,cur2,cur1} was stored at destination perm[row][i]. */
    s->cur0=(int16_t)logical[0];
    s->cur1=(int16_t)logical[1];
    s->cur2=(int16_t)logical[2];
}

int fh_stock_awb_prepare_stats(struct fh_stock_awb_commit_state *commit,
                                const uint8_t *ctx,const uint8_t *raw,uint8_t *out,
                                fh_stock_awb_sensor_gain_fn query,void *opaque)
{
    uint32_t refs[3];
    unsigned i,k;
    if(!commit||!ctx||!raw||!out)return -EINVAL;
    if(!(ctx[0x6e]&2u)){
        for(k=0;k<3;k++)commit->sensor_gain[k]=512;
    }else if(query)query(opaque,commit->sensor_gain);
    memcpy(refs,commit->sensor_gain,sizeof(refs));
    /* A missing optional query retains E0/E4/E8, it does not invent unity.
       277610's zero-denominator path calls 13AE4(8); do not crash the owner. */
    if(!refs[0]||!refs[1]||!refs[2])return -EDOM;
    for(i=0;i<9;i++){
        for(k=0;k<3;k++)w32(out+i*16+k*4,
            (uint32_t)(((uint64_t)r32(raw+i*16+k*4)<<9)/refs[k]));
        w32(out+i*16+12,r32(raw+i*16+12));
    }
    return 0;
}

int fh_stock_awb_publish_ratios(const struct fh_stock_awb_commit_state *commit,
                                      uint8_t *ctx)
{
    uint32_t packed;
    if(!commit||!ctx)return -1;
    if(commit->cur2)w16(ctx+0xa8,(uint16_t)sdiv32((int32_t)commit->cur1*4096,commit->cur2));
    if(commit->cur0)w16(ctx+0xaa,(uint16_t)sdiv32((int32_t)commit->cur1*4096,commit->cur0));
    /* Cast before the logical field shifts: stock ARM packs modulo 2^32,
       while shifting a negative signed C value would be undefined. */
    packed=(uint32_t)(int32_t)(commit->cur0>>3);
    packed+=((uint32_t)(int32_t)(commit->cur1>>3))<<10;
    packed+=((uint32_t)(int32_t)(commit->cur2>>3))<<20;
    w32(ctx+0xa4,packed);
    return 0;
}

int fh_stock_awb_publish_commit_state(const struct fh_stock_awb_commit_state *commit,uint8_t *ctx)
{
    int rc=fh_stock_awb_publish_ratios(commit,ctx);
    if(!rc)fh_stock_c9f68(ctx);
    return rc;
}

void fh_stock_awb_publish_profile(uint8_t *ctx,volatile uint32_t *regs,unsigned selector)
{
    static const uint32_t defaults[5]={0x400,0x3ffffbff,0x3ff00001,0x3feffc00,0xffbfe};
    uint16_t lo,hi;
    unsigned i;
    if(selector==0)for(i=0;i<5;i++)regs[(0x190+i*4)/4]=defaults[i];
    else if(selector==1){
        for(i=0;i<4;i++)regs[(0x190+i*4)/4]=r32(ctx+0xb4+i*4);
        regs[0x1a0/4]=r32(ctx+0xc4)&0xfffffu;
    }
    memcpy(&lo,ctx+0x7a,2);memcpy(&hi,ctx+0x78,2);
    regs[0x18c/4]=(lo&0xfffu)|((uint32_t)(hi&0xfffu)<<16);
}

struct profile_binding { uint8_t *ctx;volatile uint32_t *regs; };
static void publish_profile(void *opaque,unsigned selector)
{
    struct profile_binding *b=opaque;
    fh_stock_awb_publish_profile(b->ctx,b->regs,selector);
}

int fh_stock_awb_dispatch(struct fh_stock_awb_mode0 *s,uint8_t *ctx,
                           volatile uint32_t *regs,const uint8_t *raw,
                           struct fh_stock_ca4f4_diag *diag)
{
    struct fh_stock_awb_stat_rec rec[9];
    struct fh_stock_ca4f4_diag scratch;
    struct profile_binding binding={ctx,regs};
    int16_t gains[3]={512,512,512},last[3];
    uint32_t ac,bc,r224,r228,tail;
    uint8_t normalized[9*16];
    int rc=0,neutral;
    unsigned i,mode;
    if(!s||!ctx||!regs)return -1;
    s->dispatch_epoch++;s->last_estimator_rc=0;
    if(!(ctx[0x10]&2u))return 0;
    neutral=!!(ctx[0x6e]&4u);
    mode=ctx[0x6c]&3u;tail=r32(ctx+0x70)&0x1ffu;
    if(!neutral&&!raw)return -EINVAL;
    /* Stock initializes through CB6C4 before any periodic dispatch. Missing
     * binding is an integration error, not permission to invent a seed. */
    if(!s->commit_valid)return -EAGAIN;
    if(neutral)goto commit;
    rc=fh_stock_awb_prepare_stats(&s->commit,ctx,raw,normalized,
                                  s->sensor_query,s->sensor_gain_opaque);
    if(rc)return rc;
    if(mode>=2u||(mode==1u&&s->mode1_paused))goto tail;
    last[0]=s->commit.cur0;last[1]=s->commit.cur1;last[2]=s->commit.cur2;
    if(mode==1u){
        if(!diag)diag=&scratch;
        rc=fh_stock_ca4f4_compute(diag,ctx,regs,normalized,last,&s->mode1,
                                   publish_profile,&binding);
        s->last_estimator_rc=rc;
        if(rc<0)return rc;
        if(diag->publish_estimator){
            regs[0x4bc/4]=diag->reg4bc_next;w32(ctx+0xac,diag->ctx_ac_next);
        }
        memcpy(gains,diag->final_gain,sizeof(gains));
        goto commit;
    }
    fh_stock_awb_publish_profile(ctx,regs,0);
    for(i=0;i<9;i++){
        const uint8_t *p=normalized+i*16u;
        rec[i].c0=r32(p+0);rec[i].c1=r32(p+4);rec[i].c2=r32(p+8);rec[i].count=r32(p+12);
    }
    ac=r32(ctx+0xac);bc=regs[0x4bc/4];
    rc=fh_stock_ca13c(rec,regs[0x4d8/4],regs[0x4dc/4],(int8_t)ctx[0xdc],&bc,&ac,last,gains);
    s->last_estimator_rc=rc;
    if(rc<0)return rc;
    regs[0x4bc/4]=bc;w32(ctx+0xac,ac);
commit:
    r224=regs[0x224/4];r228=regs[0x228/4];
    fh_stock_cafc0(&s->commit,gains,ctx,regs[0x24/4],regs[0x84/4],regs[0x88/4],
                   s->sensor_gain,s->sensor_gain_opaque,&r224,&r228);
    regs[0x224/4]=r224;regs[0x228/4]=r228;
    /* CB4F0 post-commit ratios/state. */
    fh_stock_awb_publish_ratios(&s->commit,ctx);
    if(neutral)return 0;
tail:
    fh_stock_c9f68(ctx);
    regs[0x5d4/4]=(regs[0x5d4/4]&0xffff0000u)|tail;
    return 0;
}

int fh_stock_awb_mode0_tick(struct fh_stock_awb_mode0 *s,uint8_t *ctx,
                            volatile uint32_t *regs,const uint8_t *isp_vmm)
{
    if(!isp_vmm)return -1;
    return fh_stock_awb_dispatch(s,ctx,regs,isp_vmm+0x48,NULL);
}

int fh_stock_awb_mode0_tick_then_apply(struct fh_stock_awb_mode0 *s,uint8_t *ctx,
                                       volatile uint32_t *regs,const uint8_t *isp_vmm,
                                       fh_stock_awb_dependent_apply_fn apply_dependent,
                                       void *opaque)
{
    int rc;
    if(!apply_dependent)return -1;
    rc=fh_stock_awb_mode0_tick(s,ctx,regs,isp_vmm);
    if(rc)return rc;
    return apply_dependent(opaque);
}
