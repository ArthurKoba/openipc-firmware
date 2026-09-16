#include "fh8626_isp_runtime.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc,char **argv)
{
    struct fh_isp_runtime rt;
    uint32_t regs[0x1000]={0},*source;
    uint8_t *cfg=calloc(1,0x22000);
    char line[256];unsigned cases=0,skipped=0;
    FILE *f;
    assert(argc==2&&cfg);
    f=fopen(argv[1],"r");assert(f);
    fh_isp_runtime_reset(&rt);
    assert(!fh_isp_runtime_attach_mmio(&rt,regs,sizeof(regs)));
    assert(!fh_isp_runtime_attach_isp_cfg(&rt,cfg,0x22000));
    rt.ctx[0x13]=0x20;
    regs[0x30/4]=(8u<<16)|8u;
    source=(uint32_t *)(void *)(cfg+0x21250);
    while(fgets(line,sizeof(line),f)){
        unsigned x,bias,arm,expression;
        if(line[0]=='#'||line[0]=='x')continue;
        assert(sscanf(line,"%u,%u,%u,%u",&x,&bias,&arm,&expression)==4);
        assert(bias<16&&arm==expression);
        /* Current C domain requires >=64 pixels, hence x<=2^24. Larger
         * emulator-only helper vectors are not representable as this input. */
        if(x>0x1000000u){skipped++;continue;}
        source[1]=x==0x1000000u?UINT32_MAX:x*256u;
        rt.ctx[0x265]=(uint8_t)(0xa0u|bias);
        regs[0x23c/4]=0xdeadbeefu;
        assert(!fh_isp_runtime_apply_dynamic_ltm(&rt));
        assert(regs[0x23c/4]==((0xdeadbeefu&0xfffc00ffu)|arm));
        cases++;
    }
    assert(!ferror(f)&&!fclose(f));free(cfg);
    assert(cases==3360&&skipped==16);
    printf("Actual C LTM against Ghidra ARM emulator: %u vectors PASS (%u outside public domain)\n",cases,skipped);
    return 0;
}
