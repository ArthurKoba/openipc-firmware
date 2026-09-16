#include "fh8626_stock_awb_mode0_ref.h"
#include "fh8626_sensor_gc1054.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static uint8_t ctx[0x80];
static uint32_t r224,r228;
static void run(struct fh_stock_awb_commit_state *s,int16_t x,int16_t y,int16_t z)
{
    const int16_t t[3]={x,y,z};
    fh_stock_cafc0(s,t,ctx,0,0,0,NULL,NULL,&r224,&r228);
}
static struct fh_stock_awb_commit_state fresh(void)
{
    struct fh_stock_awb_commit_state s={.cur0=100,.cur1=100,.cur2=100};
    memset(ctx,0,sizeof(ctx));ctx[0x6c]=1;ctx[0x6d]=0x1f;
    r224=r228=0;
    return s;
}
static void test_hold(void)
{
    struct fh_stock_awb_commit_state s=fresh();
    s.mode=1;s.ctr0=9;s.ctr1=8;s.ctr2=7;ctx[0x6c]=40;
    run(&s,109,100,100);assert(s.cur0==100&&s.mode==1);
    assert(!s.ctr0&&!s.ctr1&&!s.ctr2);
    run(&s,110,100,100);assert(s.cur0==100&&s.ctr0==1);
    s=fresh();s.mode=1;s.ctr0=63;s.ctr1=9;s.ctr2=8;
    run(&s,169,100,100);assert(s.cur0==100&&s.ctr0==64&&!s.ctr1&&!s.ctr2);
    run(&s,169,100,100);assert(s.cur0==132&&!s.mode&&!s.ctr0);
    s=fresh();s.mode=1;s.ctr0=63;s.ctr1=31;s.ctr2=8;
    run(&s,170,100,100);assert(s.cur0==100&&s.ctr0==64&&s.ctr1==32&&!s.ctr2);
    run(&s,170,100,100);assert(s.cur0==132&&!s.mode);
    s=fresh();s.mode=1;s.ctr1=31;
    run(&s,249,100,100);assert(s.ctr1==32&&s.cur0==100);
    run(&s,249,100,100);assert(s.cur0==132&&!s.mode);
    s=fresh();s.mode=1;s.ctr0=63;s.ctr1=31;s.ctr2=15;
    run(&s,250,100,100);assert(s.ctr0==64&&s.ctr1==32&&s.ctr2==16&&s.mode==1);
    run(&s,250,100,100);assert(s.cur0==132&&!s.mode&&!s.ctr0&&!s.ctr1&&!s.ctr2);
    for(unsigned i=0;i<3;i++){
        s=fresh();s.mode=1;
        if(i==0)s.ctr0=64;
        if(i==1)s.ctr1=32;
        if(i==2)s.ctr2=16;
        run(&s,250,100,100);assert(s.cur0==132&&!s.mode);
    }
    s=fresh();s.mode=1;s.ctr0=UINT32_MAX;run(&s,101,100,100);
    assert(s.cur0==101&&s.mode==1&&!s.ctr0); /* unsigned counter comparison */
}
static void test_steps(void)
{
    struct fh_stock_awb_commit_state s=fresh();
    run(&s,130,69,100);assert(s.cur0==101&&s.cur1==68&&s.cur2==100&&!s.mode);
    s=fresh();ctx[0x6d]=0;run(&s,131,69,100);
    assert(s.cur0==102&&s.cur1==98);
    s=fresh();ctx[0x6e]=1;run(&s,900,-200,0);
    assert(s.cur0==900&&s.cur1==-200&&s.cur2==0);
    s=fresh();run(&s,101,99,101);assert(s.mode==1);
    s=fresh();run(&s,102,99,101);assert(!s.mode);
    s=fresh();s.cur0=INT16_MAX;s.cur1=INT16_MIN;
    run(&s,INT16_MIN,INT16_MAX,100);assert(s.cur0==INT16_MIN&&s.cur1==INT16_MAX&&s.mode==1);
    s=fresh();s.cur0=0;run(&s,INT16_MIN,100,100);
    assert(s.cur0==-32); /* -32768 difference has magnitude32768 */
}
struct probe { struct fh_stock_awb_commit_state *s; unsigned calls; };
static void gain_sink(void *opaque,uint32_t gain[3])
{
    struct probe *p=opaque;
    assert(gain==p->s->sensor_gain);
    assert(gain[0]==1023&&gain[1]==1023&&gain[2]==1023);
    assert(p->s->cur0==1023&&p->s->cur1==1024&&p->s->cur2==2048);
    assert(r224==0x12345678&&r228==0xabcdef01); /* before register publication */
    p->calls++;
    gain[0]=17; /* mutable persistent sensor words do not replace local ISP gains */
    ctx[0x6e]=16;ctx[0x7c]=128;ctx[0x7d]=64;ctx[0x7e]=32;
}
static void test_sensor(void)
{
    struct fh_stock_awb_commit_state s=fresh();
    struct probe p={&s,0};
    int16_t t[3]={1023,1024,2048};
    ctx[0x6e]=3;r224=0x12345678;r228=0xabcdef01;
    fh_stock_cafc0(&s,t,ctx,0,0,0,gain_sink,&p,&r224,&r228);
    assert(p.calls==1&&s.sensor_gain[0]==17);
    assert(r224==(1024u|(512u<<16))&&r228==(512u|(512u<<16)));
    s=fresh();ctx[0x6e]=3;t[0]=1022;t[1]=1023;t[2]=1024;
    fh_stock_cafc0(&s,t,ctx,0,0,0,NULL,NULL,&r224,&r228);
    assert(s.sensor_gain[0]==1022&&s.sensor_gain[1]==1023&&s.sensor_gain[2]==1023);
    assert(r224==0x02000200&&r228==0x02000200);
    ctx[0x6e]=1;run(&s,512,512,512);
    assert(s.sensor_gain[0]==1022); /* no sensor-word clear when bit1 disabled */
    fh_sensor_gc1054_awb_gain(NULL,s.sensor_gain); /* optional ABI absence */
}
static unsigned abi_calls;
static void abi_gain(uint32_t *p){assert(p[0]==7);p[2]=9;abi_calls++;}
static void test_sensor_binding(void)
{
    uint8_t table[FH_SENSOR_CB_SIZE]={0};
    struct fh_sensor_gc1054 sensor={.cb=table};
    uint32_t gain[3]={7,8,0};
    void (*fn)(uint32_t*)=abi_gain;
    /* Test adapter uses native pointer width; on target this slot is four bytes. */
    memcpy(table+0x5c,&fn,sizeof(fn));
    fh_sensor_gc1054_awb_gain(&sensor,gain);
    assert(abi_calls==1&&gain[2]==9);
}
static void test_pack(void)
{
    static const unsigned perm[4][4]={{0,1,3,2},{1,0,2,3},{3,2,0,1},{2,3,1,0}};
    const int16_t t[3]={256,512,768};
    struct fh_stock_awb_commit_state s=fresh();
    ctx[0x6e]=1;
    for(unsigned row=0;row<4;row++){
        uint32_t phys[4],logical[4]={512,1024,1536,1024};
        fh_stock_cafc0(&s,t,ctx,row<<1,0x08000800,0x08000800,NULL,NULL,&r224,&r228);
        for(unsigned i=0;i<4;i++)phys[perm[row][i]]=logical[i];
        assert(r224==(phys[0]|phys[1]<<16)&&r228==(phys[2]|phys[3]<<16));
    }
    s=fresh();ctx[0x6e]=1;
    { const int16_t edge[3]={32767,0,1};
      fh_stock_cafc0(&s,edge,ctx,0,0x0fff0fff,0x0fff0fff,NULL,NULL,&r224,&r228);
      assert(r224==0x00fff000&&r228==0x10000000); /* low32 MUL, unmasked OR */
    }
    s=fresh();ctx[0x6e]=1;run(&s,-1,0,0);
    assert(r224==0x00ffffff); /* sign-extend then MUL then logical shift */
}
static void test_day_compat(void)
{
    for(unsigned m=0;m<3;m++)for(int delta=-90;delta<=90;delta++){
        struct fh_stock_awb_commit_state a=fresh(),b;
        const int16_t t[3]={(int16_t)(100+delta),100,(int16_t)(100-delta)};
        uint32_t a0,a1,b0,b1;
        ctx[0x6d]=15;a.mode=m;a.ctr0=100;a.ctr1=20;a.ctr2=3;b=a;
        fh_stock_cafc0_day(&a,t,2,0x120034,0x560078,&a0,&a1);
        fh_stock_cafc0(&b,t,ctx,2,0x120034,0x560078,NULL,NULL,&b0,&b1);
        assert(!memcmp(&a,&b,sizeof(a))&&a0==b0&&a1==b1);
    }
}
int main(void)
{
    test_hold();test_steps();test_sensor();test_sensor_binding();test_pack();test_day_compat();
    puts("test_cafc0_controls: PASS");
    return 0;
}
