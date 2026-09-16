#include "fh8626_isp_runtime.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int64_t signed_word(uint32_t v){return v<0x80000000u?v:(int64_t)v-INT64_C(4294967296);}
static uint32_t next(uint32_t *v){*v=*v*1664525u+1013904223u;return *v;}
static uint32_t smooth(uint32_t old,uint32_t input,unsigned divisor)
{return (uint32_t)((int64_t)old+signed_word(input-old)/(int64_t)divisor);}
int main(void)
{
    struct fh_isp_runtime rt;
    uint32_t regs[0x1000]={0},seed=123;
    uint8_t *cfg=calloc(1,0x22000);uint32_t *source;
    assert(cfg);
    fh_isp_runtime_reset(&rt);
    assert(!fh_isp_runtime_attach_mmio(&rt,regs,sizeof(regs)));
    assert(!fh_isp_runtime_attach_isp_cfg(&rt,cfg,0x22000));
    rt.ctx[0x13]=0x20;rt.ctx[0x66]=64;
    rt.ctx[0x268]=rt.ctx[0x269]=rt.ctx[0x26a]=64;
    regs[0x30/4]=(8u<<16)|8u; /* 64 pixels; small but valid divide domain */
    source=(uint32_t *)(void *)(cfg+0x21250);
    for(unsigned n=0;n<10000;n++){
        struct fh_isp_stock_d0460_record old,previous;
        uint32_t q,expected,avg;
        uint32_t *h=(uint32_t *)(void *)rt.d0460_state.rec;
        for(unsigned i=0;i<sizeof(rt.d0460_state)/4;i++)h[i]=next(&seed);
        old=rt.d0460_state.rec[3];previous=rt.d0460_state.rec[1];
        source[0]=next(&seed);source[1]=UINT32_MAX;source[2]=next(&seed);
        q=(uint32_t)(((((uint64_t)old.reserved_zero<<32)|old.area_q11)<<3)/64u);
        expected=smooth(q,3072,3);
        assert(!fh_isp_runtime_apply_dynamic_ltm(&rt));
        avg=(previous.base_200+rt.d0460_state.rec[1].base_200)>>1;
        assert(rt.d0460_state.rec[3].base_200==smooth(old.base_200,avg,8));
        avg=(previous.base_e00+rt.d0460_state.rec[1].base_e00)>>1;
        assert(rt.d0460_state.rec[3].base_e00==smooth(old.base_e00,avg,3));
        assert(rt.d0460_state.rec[3].area_q11==(expected>>3)*64u);
        assert(!rt.d0460_state.rec[3].reserved_zero);
        assert((regs[0x23c/4]&0x3ff00u)==0x3ff00u);
    }
    free(cfg);puts("LTM word-wrap/large-conversion edges: PASS");return 0;
}
