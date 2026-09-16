#include "fh8626_stock_awb_mode0_pipeline.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static uint32_t rd(const uint8_t *p){uint32_t v;memcpy(&v,p,4);return v;}
static void wr(uint8_t *p,uint32_t v){memcpy(p,&v,4);}
static void init(struct fh_stock_awb_mode0 *s,uint8_t *ctx,uint32_t *regs,uint32_t *raw)
{
    unsigned i;
    memset(s,0,sizeof(*s));memset(ctx,0,0x200);memset(regs,0,0x600);memset(raw,0,9*16);
    s->commit_valid=1;s->commit.cur0=s->commit.cur1=s->commit.cur2=512;
    ctx[0x10]=2;ctx[0x6c]=1;ctx[0x6e]=1;ctx[0x74]=255;
    ctx[0x75]=1;ctx[0x76]=2;ctx[0x77]=3;
    regs[0x224/4]=regs[0x228/4]=0x02000200;
    regs[0x4d8/4]=0x01000100;regs[0x4dc/4]=0x100;
    regs[0x4bc/4]=0xabcd0321;regs[0x5d4/4]=0x456700aa;
    wr(ctx+0xac,0xda123456);wr(ctx+0x70,0x145);
    for(i=0;i<9;i++){raw[i*4]=i<6?500:2000;raw[i*4+1]=raw[i*4+2]=1000;raw[i*4+3]=100;}
    for(i=0;i<5;i++)wr(ctx+0xb4+i*4,0x12340000+i);
}
static unsigned queries;
static void query(void *opaque,uint32_t refs[3])
{
    uint8_t *ctx=opaque;
    queries++;refs[0]=refs[1]=refs[2]=512;
    ctx[0x6c]=1;wr(ctx+0x70,0x1ab);
}
static void branches(void)
{
    struct fh_stock_awb_mode0 s;
    uint8_t ctx[0x200],before[0x200];
    uint32_t regs[0x600/4],rb[0x600/4],raw[36];
    init(&s,ctx,regs,raw);ctx[0x10]=0;s.dispatch_epoch=UINT32_MAX;
    memcpy(before,ctx,sizeof(ctx));memcpy(rb,regs,sizeof(regs));
    assert(!fh_stock_awb_dispatch(&s,ctx,regs,NULL,NULL));
    assert(!s.dispatch_epoch&&!memcmp(ctx,before,sizeof(ctx))&&!memcmp(rb,regs,sizeof(regs)));
    ctx[0x10]=2;ctx[0x6e]=5;wr(ctx+0xb0,0xaabbccdd);
    s.sensor_query=query;s.sensor_gain_opaque=ctx;queries=0;
    assert(!fh_stock_awb_dispatch(&s,ctx,regs,NULL,NULL));
    assert(!queries&&rd(ctx+0xb0)==0xaabbccdd&&regs[0x5d4/4]==0x456700aa);
    assert(regs[0x224/4]==0x02000200&&regs[0x4bc/4]==0xabcd0321);
    assert(rd(ctx+0xa8)==0x10001000);
    ctx[0x6e]=2;ctx[0x6c]=2;
    assert(!fh_stock_awb_dispatch(&s,ctx,regs,(uint8_t *)raw,NULL));
    assert(queries==1&&ctx[0x6c]==1&&rd(ctx+0x70)==0x1ab);
    assert(regs[0x5d4/4]==0x45670145&&regs[0x224/4]==0x02000200);
    init(&s,ctx,regs,raw);ctx[0x6c]=0;memset(raw,0,sizeof(raw));
    assert(!fh_stock_awb_dispatch(&s,ctx,regs,(uint8_t *)raw,NULL));
    assert(s.last_estimator_rc==1&&s.commit.cur0==384&&s.commit.cur1==384&&s.commit.cur2==384);
    assert(regs[0x190/4]==0x400&&regs[0x194/4]==0x3ffffbff&&regs[0x1a0/4]==0xffbfe);
    assert(rd(ctx+0xac)==0xc0000000);
}
static void recovery(void)
{
    struct fh_stock_awb_mode0 s;
    struct fh_stock_ca4f4_diag d;
    uint8_t ctx[0x200];uint32_t regs[0x600/4],raw[36],empty[36]={0};
    int16_t last[3]={500,510,520};unsigned i;
    init(&s,ctx,regs,raw);
    assert(!fh_stock_ca4f4_compute(&d,ctx,regs,(uint8_t *)raw,last,&s.mode1,NULL,NULL));
    assert(d.weight==8&&d.profile_selector==1&&d.publish_estimator);
    s.mode1.recovery_count=9;
    assert(fh_stock_ca4f4_compute(&d,ctx,regs,(uint8_t *)empty,last,&s.mode1,NULL,NULL)==1);
    assert(s.mode1.recovery_active==1&&s.mode1.recovery_count==9);
    assert(!d.publish_estimator&&d.reg4bc_next==regs[0x4bc/4]&&d.ctx_ac_next==rd(ctx+0xac));
    assert(!memcmp(d.final_gain,last,sizeof(last))&&d.profile_selector==1);
    assert(!fh_stock_ca4f4_compute(&d,ctx,regs,(uint8_t *)raw,last,&s.mode1,NULL,NULL));
    assert(s.mode1.recovery_count==10&&d.weight==0&&d.profile_selector==-1&&d.publish_estimator);
    ctx[0x74]=1;
    assert(fh_stock_ca4f4_compute(&d,ctx,regs,(uint8_t *)raw,last,&s.mode1,NULL,NULL)==2);
    assert(s.mode1.recovery_count==11&&!d.publish_estimator&&d.profile_selector==-1);
    assert(d.reg4bc_next==regs[0x4bc/4]);
    s.mode1.recovery_count=20;
    assert(!fh_stock_ca4f4_compute(&d,ctx,regs,(uint8_t *)raw,last,&s.mode1,NULL,NULL));
    assert(!s.mode1.recovery_count&&!s.mode1.recovery_active&&d.weight==8&&d.profile_selector==-1);
    init(&s,ctx,regs,raw);
    assert(!fh_stock_awb_dispatch(&s,ctx,regs,(uint8_t *)empty,&d));
    assert(regs[0x4bc/4]==0xabcd0321&&rd(ctx+0xac)==0xda123456);
    assert(regs[0x190/4]==rd(ctx+0xb4)&&regs[0x1a0/4]==(rd(ctx+0xc4)&0xfffff));
    for(i=0;i<9;i++){raw[i*4]=4096;raw[i*4+1]=4096;raw[i*4+2]=UINT32_MAX;}
    assert(fh_stock_ca4f4_compute(&d,ctx,regs,(uint8_t *)raw,last,&s.mode1,NULL,NULL)==1);
    assert(!d.valid_count); /* all three division sentinel checks matter */
    init(&s,ctx,regs,raw);
    for(i=0;i<9;i++)raw[i*4]=100000;
    assert(!fh_stock_ca4f4_compute(&d,ctx,regs,(uint8_t *)raw,last,&s.mode1,NULL,NULL));
    assert(d.ratio_min==0x10000);
}
static int32_t s16(uint32_t v){v&=65535;return v<32768?(int32_t)v:(int32_t)v-65536;}
static uint32_t rnd(uint32_t *v){*v=*v*1664525u+1013904223u;return *v;}
static void arithmetic(void)
{
    uint32_t seed=7,i,j;
    for(i=0;i<10000;i++){
        int16_t in[3],out[3],want[3];
        uint32_t d8=rnd(&seed),dc=rnd(&seed),bc=0xcafe9999,q[3],total;
        int8_t bias=(int8_t)(rnd(&seed)&127);int32_t mn,ctrl;
        for(j=0;j<3;j++){in[j]=(int16_t)s16(rnd(&seed));if(!in[j])in[j]=1;}
        q[0]=((d8>>7)&0x1ffe00u)/(uint32_t)(int32_t)in[1];
        q[1]=((d8<<9)&0x1ffe00u)/(uint32_t)(int32_t)in[0];
        q[2]=((dc<<9)&0x1ffe00u)/(uint32_t)(int32_t)in[2];
        total=q[0]+q[1]+q[2];
        for(j=0;j<3;j++){
            int64_t product=(int64_t)s16(total)*in[j]*64;
            want[j]=(int16_t)s16((uint32_t)product>>16);
        }
        mn=want[0]<want[1]?want[0]:want[1];if(want[2]<mn)mn=want[2];
        ctrl=(((mn*1023)>>9)*(bias+128))>>7;
        if(ctrl<0)ctrl=0;
        if(ctrl>1023)ctrl=1023;
        assert(!fh_stock_awb_normalize_targets(in,d8,dc,bias,&bc,out));
        assert(!memcmp(out,want,sizeof(out))&&bc==(0xcafe0000u|(uint32_t)ctrl));
    }
}
static void snapshot(void)
{
    struct fh_stock_awb_mode0 s,expected;
    struct fh_stock_awb_snapshot snap;
    uint8_t ctx[0x200],old[16];uint32_t regs[0x600/4],raw[36];
    init(&s,ctx,regs,raw);s.mode1.recovery_count=7;s.mode1.recovery_active=1;
    expected=s;memcpy(old,ctx+0xa4,16);
    memset(ctx+0x140,0x53,24);
    fh_stock_awb_snapshot_save(&snap,&s,ctx,regs,2,3);
    memset(ctx+0x140,0,24);
    memset(ctx+0xa4,0,16);memset(&s,0,sizeof(s));s.dispatch_epoch=88;
    ctx[0x10]=0xa0;ctx[0x6c]=0xf0;
    regs[0x190/4]=0xabcdef;regs[0x4bc/4]=0;
    assert(fh_stock_awb_snapshot_restore(&snap,&s,ctx,regs,2,4)==-ESTALE);
    assert(regs[0x190/4]==0xabcdef&&rd(ctx+0xac)==0&&snap.valid);
    assert(!fh_stock_awb_snapshot_restore(&snap,&s,ctx,regs,2,3));
    expected.dispatch_epoch=88;
    assert(!memcmp(&s,&expected,sizeof(s))&&!memcmp(old,ctx+0xa4,16));
    assert(ctx[0x10]==0xa2&&ctx[0x6c]==0xf1);
    for(unsigned i=0;i<24;i++)assert(ctx[0x140+i]==0x53);
    assert(regs[0x4bc/4]==0xabcd0321&&regs[0x190/4]==0&&!snap.valid);
}
static void startup(void)
{
    struct fh_stock_awb_mode0 s,want;
    uint8_t ctx[0x200],oldctx[0x200];uint32_t regs[0x600/4],oldregs[0x600/4],raw[36];
    const uint32_t triplet[3]={0x10000800u,0x8000u,0xffffu};
    init(&s,ctx,regs,raw);s.commit_valid=0;
    memcpy(oldctx,ctx,sizeof(ctx));memcpy(oldregs,regs,sizeof(regs));
    assert(fh_stock_awb_dispatch(&s,ctx,regs,(uint8_t *)raw,NULL)==-EAGAIN);
    assert(!s.commit_valid&&!memcmp(ctx,oldctx,sizeof(ctx))&&!memcmp(regs,oldregs,sizeof(regs)));
    s.commit.ctr0=11;s.commit.ctr1=12;s.commit.ctr2=13;s.commit.mode=14;
    s.commit.sensor_gain[0]=501;s.commit.sensor_gain[1]=502;s.commit.sensor_gain[2]=503;
    s.mode1.recovery_active=1;s.mode1.recovery_count=9;s.dispatch_epoch=71;
    want=s;want.commit_valid=1;want.commit.cur0=2048;want.commit.cur1=INT16_MIN;want.commit.cur2=-1;
    fh_stock_awb_init_triplet(&s,triplet);
    assert(!memcmp(&s,&want,sizeof(s)));
    /* An explicit rebind preserves every unrelated history/control field. */
    fh_stock_awb_init_triplet(&s,triplet);
    assert(!memcmp(&s,&want,sizeof(s)));
}
int main(void){branches();recovery();arithmetic();snapshot();startup();puts("AWB dispatch/recovery/normalization/snapshot/startup: PASS");return 0;}
