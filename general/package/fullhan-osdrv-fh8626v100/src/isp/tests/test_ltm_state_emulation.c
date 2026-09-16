#include "fh8626_isp_runtime.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t seed;
static uint32_t next(void){seed=seed*1664525u+1013904223u;return seed;}
int main(int argc,char **argv)
{
    const unsigned dims[4][2]={{8,8},{1280,720},{2047,2047},{1,64}};
    const unsigned grids[5][2]={{0,0},{1,1},{3,2},{15,15},{31,31}};
    const unsigned offsets[6]={0x23c,0x240,0x244,0x248,0x24c,0x250};
    const unsigned fields[5]={0x66,0x265,0x268,0x269,0x26a};
    uint8_t *cfg=calloc(1,0x22000);char line[2048];unsigned cases=0;FILE *f;
    assert(argc==2&&cfg);f=fopen(argv[1],"r");assert(f);
    while(fgets(line,sizeof(line),f)){
        struct fh_isp_runtime rt;uint32_t regs[0x1000]={0},row[32],*h;
        unsigned n=0,c;char *token;
        if(line[0]=='#'||line[0]=='c')continue;
        for(token=strtok(line,",");token;token=strtok(NULL,",")){
            char *end;unsigned long value=strtoul(token,&end,10);
            assert(n<32&&end!=token&&(*end==0||*end=='\n')&&value<=UINT32_MAX);
            row[n++]=(uint32_t)value;
        }
        assert(n==32);c=row[0];assert(c==cases&&row[1]==0x81234567u+c);seed=row[1];
        fh_isp_runtime_reset(&rt);
        assert(!fh_isp_runtime_attach_mmio(&rt,regs,sizeof(regs)));
        assert(!fh_isp_runtime_attach_isp_cfg(&rt,cfg,0x22000));
        rt.ctx[0x13]=0x20;
        regs[0x30/4]=(dims[c%4][1]<<16)|dims[c%4][0];
        regs[0x1cc/4]=(grids[c%5][1]<<24)|(grids[c%5][0]<<16);
        for(unsigned k=0;k<0x7000;k+=4){uint32_t v=(k%64==4&&c%2==0)?0:next();memcpy(cfg+0x5c8+k,&v,4);}
        for(unsigned k=0;k<3;k++){uint32_t v=next();memcpy(cfg+0x21250+k*4,&v,4);}
        h=(uint32_t *)(void *)rt.d0460_state.rec;
        for(unsigned k=0;k<24;k++)h[k]=next();
        for(unsigned k=0;k<5;k++)rt.ctx[fields[k]]=(uint8_t)next();
        for(unsigned k=0;k<6;k++)regs[offsets[k]/4]=next();
        assert(!fh_isp_runtime_apply_dynamic_ltm(&rt));
        for(unsigned k=0;k<24;k++){
            if(h[k]!=row[k+2])fprintf(stderr,"case%u state%u C=%08x ARM=%08x\n",c,k,h[k],row[k+2]);
            assert(h[k]==row[k+2]);
        }
        for(unsigned k=0;k<6;k++){
            if(regs[offsets[k]/4]!=row[k+26])fprintf(stderr,"case%u reg%x C=%08x ARM=%08x\n",c,offsets[k],regs[offsets[k]/4],row[k+26]);
            assert(regs[offsets[k]/4]==row[k+26]);
        }
        cases++;
    }
    assert(!ferror(f)&&!fclose(f)&&cases==32);free(cfg);
    puts("Full C LTM against Ghidra ARM state/register vectors: 32 PASS");
    return 0;
}
