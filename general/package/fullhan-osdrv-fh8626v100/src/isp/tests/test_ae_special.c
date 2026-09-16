#include "fh8626_ae_runtime.h"
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>

struct events { unsigned n, pair[3]; uint32_t value[3]; int fail; };
static int stage(void *opaque, unsigned pair, uint32_t value)
{
    struct events *e = opaque;
    assert(e->n < 3u);
    e->pair[e->n] = pair; e->value[e->n++] = value;
    return pair == 1u ? e->fail : 0;
}
int main(void)
{
    struct fh8626_ae_runtime ae;
    struct events e = {0};
    uint8_t ctx[0xa80] = {0};
    const uint32_t seed[6] = {11u,22u,33u,44u,55u,66u};
    const uint32_t counters[] = {0u,1u,5u,0xfffffffbu,0x7fffffffu,0x80000000u,0xffffffffu};
    fh8626_ae_runtime_init_passive(&ae,ctx,NULL,NULL,NULL,NULL,NULL,NULL);
    for(unsigned i=0;i<6;i++) assert(ae.special_queue[i]==64u);
    ae.stage=stage; ae.commit_opaque=&e;
    assert(fh8626_ae_runtime_enable_observe(&ae,1)==0);
    for(unsigned branch=0;branch<2;branch++) {
        for(unsigned index=0;index<4;index++) {
            for(unsigned c=0;c<sizeof(counters)/sizeof(counters[0]);c++) {
                uint32_t expected[7]={11,22,33,44,55,66,0};
                unsigned base=branch?0u:3u;
                int64_t signed_c=counters[c] < 0x80000000u ? counters[c]
                    : (int64_t)counters[c]-0x100000000LL;
                unsigned fifth=signed_c%5==0;
                uint32_t value=branch?(fifth?128u:64u):(fifth?64u:128u);
                uint32_t head=index?seed[base]:value;
                ctx[0x2c]=0x10u|(branch?(0x20u|(index<<6)):0u);
                ctx[0x2d]=1u|(index<<1);
                ctx[0x2f]=0u; /* C9240 is independent of normal signed selector */
                assert(fh8626_ae_runtime_enable_commit(&ae,1)==0);
                assert(fh8626_ae_runtime_seed_special(&ae,seed,counters[c])==0);
                expected[base+index]=value;
                memmove(expected+base,expected+base+1,index*sizeof(uint32_t));
                e.n=0; e.fail=-EIO;
                ae.queue[0].value=777u; ae.queue[0].dirty=1u;
                ae.history[0]=123;
                assert(fh8626_ae_runtime_special_step(&ae)==-EIO);
                assert(e.n==3u);
                assert(e.pair[0]==1u && e.value[0]==(branch?head:64u));
                assert(e.pair[1]==2u && e.value[1]==(branch?64u:head));
                assert(e.pair[2]==0u && e.value[2]==(branch?(fifth?200u:400u):(fifth?400u:200u)));
                assert(ae.special_counter==counters[c]+1u);
                assert(memcmp(expected,ae.special_queue,sizeof(expected))==0);
                assert(ae.queue[0].value==777u&&ae.queue[0].dirty==1u&&ae.history[0]==123);
            }
        }
    }
    ctx[0x2c]=0x10u; ctx[0x2d]=0u;
    e.n=0; ae.special_counter=0xffffffffu;
    assert(fh8626_ae_runtime_special_step(&ae)==0);
    assert(e.n==0u&&ae.special_counter==0u);
    assert(fh8626_ae_runtime_enable_commit(&ae,0)==0);
    assert(fh8626_ae_runtime_special_step(&ae)==0);
    assert(ae.special_counter==0u);
    ctx[0x2c]=0;
    assert(fh8626_ae_runtime_enable_commit(&ae,1)==0);
    return 0;
}
