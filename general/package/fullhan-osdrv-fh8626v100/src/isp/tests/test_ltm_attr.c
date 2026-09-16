#include "fh8626_isp_runtime.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static uint32_t next(uint32_t *s){*s=*s*1664525u+1013904223u;return *s;}
static uint32_t word(const uint8_t *p){uint32_t v;memcpy(&v,p,4);return v;}
static void putword(uint8_t *p,uint32_t v){memcpy(p,&v,4);}
int main(void)
{
    struct fh_isp_runtime rt;
    struct fh_isp_ltm_attr attr,out;
    uint32_t regs[0x1000],oldregs[0x1000],seed=21;
    uint8_t in[0x50],expected[0x50],ctx[FH_ISP_CTX_SIZE];
    const unsigned dst[11]={0x262,0x263,0x264,0x265,0x266,0x268,0x269,0x26a,0,0,0};
    unsigned n,i;
    fh_isp_runtime_reset(&rt);
    assert(!fh_isp_runtime_attach_mmio(&rt,regs,sizeof(regs)));
    assert(fh_isp_runtime_get_ltm_attr(&rt,NULL)==-3002);
    assert(fh_isp_runtime_set_ltm_attr(&rt,NULL)==-3002);
    assert(fh_isp_runtime_get_ltm_attr(NULL,&out)==-EINVAL);
    for(n=0;n<2000;n++){
        for(i=0;i<sizeof(rt.ctx);i++)rt.ctx[i]=(uint8_t)(next(&seed)>>16);
        for(i=0;i<sizeof(in);i++)in[i]=(uint8_t)(next(&seed)>>24);
        /* Include both states and unsigned high-bit clipping. */
        if(n&1){putword(in,n&1);putword(in+4,(n>>1)&1);}
        for(i=0;i<sizeof(regs)/sizeof(regs[0]);i++)regs[i]=next(&seed);
        memcpy(oldregs,regs,sizeof(regs));memcpy(ctx,rt.ctx,sizeof(ctx));
        memcpy(expected,in,sizeof(in));
        if(word(expected)>1)putword(expected,1);
        if(word(expected+4)>1)putword(expected+4,1);
        if(expected[8]>3)expected[8]=3;
        if(expected[9]>15)expected[9]=15;
        if(expected[10]>31)expected[10]=31;
        if(expected[14]>15)expected[14]=15;
        for(i=0x43;i<0x4f;i++)if(expected[i]>15)expected[i]=15;
        ctx[0x260]=(uint8_t)(word(expected)|(word(expected+4)<<1)|(expected[8]<<2)|(expected[9]<<4));
        ctx[0x261]=(ctx[0x261]&224)|expected[10];
        for(i=0;i<8;i++)if(i!=3)ctx[dst[i]]=expected[11+i];
        ctx[0x265]=(ctx[0x265]&240)|expected[14];
        memcpy(ctx+0x26c,expected+0x13,48);
        for(i=0;i<6;i++)ctx[0x29c+i]=expected[0x43+2*i]|(expected[0x44+2*i]<<4);
        rt.params_dirty=17;
        memcpy(&attr,in,sizeof(attr));
        assert(!fh_isp_runtime_set_ltm_attr(&rt,&attr));
        assert(!memcmp(&attr,expected,sizeof(attr)));
        assert(!memcmp(rt.ctx,ctx,sizeof(ctx))&&!memcmp(regs,oldregs,sizeof(regs))&&rt.params_dirty==17);
        memset(&out,0x96,sizeof(out));
        assert(!fh_isp_runtime_get_ltm_attr(&rt,&out));
        assert(!memcmp(&out,expected,0x4f)&&out.reserved_4f==0x96);
        assert(!memcmp(rt.ctx,ctx,sizeof(ctx))&&!memcmp(regs,oldregs,sizeof(regs)));
    }
    /* Hardware enable, requested enable and update scheduling are distinct. */
    for(i=0;i<2;i++){
        uint8_t saved260;
        rt.ctx[0x13]=(uint8_t)(0x8a|(i<<5));
        saved260=rt.ctx[0x260];
        memcpy(ctx,rt.ctx,sizeof(ctx));memcpy(oldregs,regs,sizeof(regs));
        assert(!fh_isp_runtime_set_ltm_enabled(&rt,0));
        ctx[0x260]=saved260&~2u;oldregs[0x24/4]|=0x40000u;
        assert(!memcmp(rt.ctx,ctx,sizeof(ctx))&&!memcmp(regs,oldregs,sizeof(regs)));
        assert(!fh_isp_runtime_set_ltm_enabled(&rt,1));
        ctx[0x260]=saved260|2u;oldregs[0x24/4]&=~0x40000u;
        assert(!memcmp(rt.ctx,ctx,sizeof(ctx))&&!memcmp(regs,oldregs,sizeof(regs)));
    }
    puts("LTM public ABI/clipping/roundtrip/hardware gate: PASS");
    return 0;
}
