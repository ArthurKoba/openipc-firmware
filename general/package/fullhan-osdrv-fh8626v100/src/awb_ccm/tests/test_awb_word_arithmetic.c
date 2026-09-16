#include "fh8626_stock_c9f68_ref.h"
#include "fh8626_stock_awb_mode0_pipeline.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
static int64_t s32(uint32_t x){return x<0x80000000u?x:(int64_t)x-4294967296LL;}
static int64_t s16(uint32_t x){x&=65535;return x<32768?x:(int64_t)x-65536;}
static uint32_t get32(const uint8_t *p){uint32_t v;memcpy(&v,p,4);return v;}
static void put32(uint8_t *p,uint32_t v){memcpy(p,&v,4);}
static uint32_t log_oracle(uint32_t x)
{
    unsigned n=0;uint64_t power=1;
    if(!x)return 0xffffff00;
    while(power*2<=x){power*=2;n++;}
    return n*256+(uint32_t)(((uint64_t)x-power)*256/power);
}
static uint32_t convert(uint32_t p)
{
    uint32_t a=p&65535,b=p>>16,m=(a+b+8194)/4;
    int64_t common=log_oracle(m)&65535;
    uint32_t x=(uint32_t)((int64_t)(log_oracle(a)&65535)-common)&65535;
    uint32_t y=(uint32_t)((int64_t)(log_oracle(b)&65535)-common)&65535;
    return x<<16|y;
}
static uint32_t dist(uint32_t a,uint32_t b)
{
    int64_t x=s16(a>>16)-s16(b>>16),y=s16(a)-s16(b);
    return (uint32_t)(x*x+y*y);
}
static int oracle(uint8_t *ctx,int wide)
{
    uint32_t idx[4]={0,1,2,3},point[4],d[4],cur;
    uint16_t a8,aa,b0;
    memcpy(&a8,ctx+0xa8,2);memcpy(&aa,ctx+0xaa,2);
    cur=convert(a8|((uint32_t)aa<<16));
    for(unsigned i=0;i<4;i++){point[i]=convert(get32(ctx+0x88+4*i));d[i]=dist(point[i],cur);}
    for(unsigned i=0;i<3;i++){
        unsigned k=i;
        for(unsigned j=i+1;j<4;j++)if(s32(d[j])<s32(d[k]))k=j;
        uint32_t t=d[k];d[k]=d[i];d[i]=t;t=idx[k];idx[k]=idx[i];idx[i]=t;
    }
    uint32_t p=point[idx[0]],q=point[idx[1]];
    int64_t px=s16(cur>>16)-s16(p>>16),py=s16(cur)-s16(p);
    int64_t vx=s16(q>>16)-s16(p>>16),vy=s16(q)-s16(p);
    int64_t dot=px*vx+py*vy;unsigned second=idx[1];
    if(idx[0]!=0&&idx[0]!=3&&s32((uint32_t)dot)<0){
        second=idx[2];q=point[second];
        vx=s16(q>>16)-s16(p>>16);vy=s16(q)-s16(p);
        dot=px*vx+py*vy;
    }
    int64_t den=s32((uint32_t)(vx*vx+vy*vy));if(den<1)den=1;
    int64_t num=wide?dot*256:s32((uint32_t)(dot*256));
    int64_t w=num/den;if(w>255)w=256;if(w<0)w=0;
    ctx[0xb1]=(ctx[0xb1]&15)|(idx[0]<<4);
    ctx[0xb2]=(ctx[0xb2]&240)|second;
    memcpy(&b0,ctx+0xb0,2);b0=(b0&0xf000)|(uint16_t)w;memcpy(ctx+0xb0,&b0,2);
    return (int)w;
}
static uint32_t rng=0x86262026;
static uint32_t random32(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static void test_math(void)
{
    unsigned differences=0;
    for(uint32_t x=0;x<65536;x++)assert(fh_stock_d27c4(x)==log_oracle(x));
    assert(fh_stock_d27c4(0xffffffff)==8191);
    assert(fh_stock_c9f30(0x80008000,0x7fff7fff)==0xfffc0002u);
    for(unsigned t=0;t<20000;t++){
        uint8_t actual[0xb4]={0},want[0xb4],wide[0xb4];
        for(unsigned i=0;i<4;i++){
            uint32_t a=random32();
            if(t%3==0)a&=0xffff0000u;
            if(t%7==0)a=0x10001000;
            put32(actual+0x88+i*4,a);
            assert(fh_stock_c9db0(a)==convert(a));
        }
        uint32_t cur=random32();
        if(t%2==0)cur&=0xffff0000u;
        put32(actual+0xa8,cur);put32(actual+0xb0,0xcfa5a123);
        if(t==0){
            put32(actual+0x88,0x00010001);
            for(unsigned i=1;i<4;i++)put32(actual+0x88+i*4,0xffffffff);
            put32(actual+0xa8,0x00320032);
        }
        memcpy(want,actual,sizeof(want));memcpy(wide,actual,sizeof(wide));
        int exact=oracle(want,0),old=oracle(wide,1);differences+=(exact!=old);
        fh_stock_c9f68(actual);
        assert(!memcmp(actual,want,sizeof(actual)));
        uint32_t a=random32(),b=random32();
        assert(fh_stock_c9f30(a,b)==dist(a,b));
    }
    assert(differences>0);
    printf("word arithmetic: PASS, %u wide-Q8 mismatches detected in 20000 vectors\n",differences);
}
static unsigned queries;
static void query(void *opaque,uint32_t refs[3])
{
    assert(opaque==&queries);queries++;
    refs[0]=256;refs[1]=1024;refs[2]=1;
}
static void test_normalization(void)
{
    struct fh_stock_awb_commit_state s={0};
    uint8_t ctx[0x80]={0},raw[144],out[144],before[144];
    for(unsigned i=0;i<9;i++){
        put32(raw+i*16,100+i);put32(raw+i*16+4,201+i);
        put32(raw+i*16+8,0x80000001u+i);put32(raw+i*16+12,10+i);
    }
    assert(!fh_stock_awb_prepare_stats(&s,ctx,raw,out,query,&queries));
    assert(!queries&&!memcmp(out,raw,sizeof(raw))&&s.sensor_gain[0]==512);
    ctx[0x6e]=2;assert(!fh_stock_awb_prepare_stats(&s,ctx,raw,out,query,&queries));
    assert(queries==1);
    for(unsigned i=0;i<9;i++){
        assert(get32(out+i*16)==2*(100+i));
        assert(get32(out+i*16+4)==(201+i)/2);
        assert(get32(out+i*16+8)==(uint32_t)(((uint64_t)(0x80000001u+i))<<9));
        assert(get32(out+i*16+12)==10+i);
    }
    memcpy(before,out,sizeof(out));
    assert(!fh_stock_awb_prepare_stats(&s,ctx,raw,out,NULL,NULL));
    assert(!memcmp(before,out,sizeof(out)));
    s.sensor_gain[1]=0;
    assert(fh_stock_awb_prepare_stats(&s,ctx,raw,out,NULL,NULL)==-EDOM);
    assert(!memcmp(before,out,sizeof(out))); /* all-output unchanged on bad divisor */
    assert(fh_stock_awb_prepare_stats(NULL,ctx,raw,out,NULL,NULL)==-EINVAL);
    struct fh_stock_awb_commit_state seed;
    fh_stock_awb_commit_seed_bayer(&seed,0,0x08000800,0x08000800,0x04000200,0x06000400);
    assert(seed.cur0==256&&seed.cur1==512&&seed.cur2==768);
}
int main(void){test_math();test_normalization();puts("test_awb_word_arithmetic: PASS");return 0;}
