#include "fh8626_isp_runtime.h"
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
int main(int argc,char **argv)
{
    static struct fh_isp_runtime rt;
    static uint32_t mmio[0x8000/4],expected[0x8000/4];
    char line[200];unsigned pc,off,val,n=0,i;
    FILE *f;
    assert(argc==2);f=fopen(argv[1],"r");assert(f);
    for(i=0;i<sizeof(mmio)/sizeof(mmio[0]);i++)mmio[i]=expected[i]=0xdeadbeefu;
    rt.mmio=mmio;
    while(fgets(line,sizeof(line),f)){
        if(line[0]=='#')continue;
        assert(sscanf(line,"%x,%x,%x",&pc,&off,&val)==3);
        assert(pc>=0xc4998u&&pc<0xc5058u&&!(off&3u)&&off<0x1000u);
        expected[off/4]=val;n++;
    }
    assert(!ferror(f)&&fclose(f)==0&&n==236);
    assert(fh_isp_runtime_apply_c4998_static_defaults(&rt)==0);
    for(i=0;i<sizeof(mmio)/sizeof(mmio[0]);i++){
        if(mmio[i]!=expected[i]){
            fprintf(stderr,"Init prefix mismatch offset=%x C=%08x ARM=%08x\n",i*4,mmio[i],expected[i]);
            return 1;
        }
    }
    /* Full current startup still has unresolved initial gamma binding.
     * Check only proven tail fields, not full stock Init parity. */
    memset(&rt,0,sizeof(rt));rt.mmio=mmio;
    assert(fh_isp_runtime_apply_known_stock_init(&rt,1280,720)==0);
    assert(mmio[0x178/4]==0x44u);
    /* Subsequent C531C clears format bits for this zero-format fixture. */
    assert(mmio[0x24/4]==(0x0a71eb14u&~0x1eu));
    puts("C4998 prefix236 ARM writes and deferred tail: PASS");
    return 0;
}
