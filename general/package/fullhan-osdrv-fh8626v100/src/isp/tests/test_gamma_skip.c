#include "fh8626_isp_runtime.h"
#include "fh8626_cf8ec_gamma_presets.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    struct fh_isp_runtime rt;uint32_t regs[0x1000]={0};
    uint8_t old[0x280];
    for(unsigned hi=0;hi<16;hi++)for(unsigned lo=0;lo<16;lo++){
        fh_isp_runtime_reset(&rt);assert(!fh_isp_runtime_attach_mmio(&rt,regs,sizeof(regs)));
        rt.ctx[0x12]=0x40;rt.ctx[0x364]=(hi<<4)|lo;rt.params_dirty=1;
        memset(rt.ctx+0x5f0,0xa6,0x140);memset(rt.ctx+0x730,0xb7,0x140);
        memcpy(old,rt.ctx+0x5f0,sizeof(old));
        assert(!fh_isp_runtime_apply_cfbc4(&rt));
        assert(!memcmp(rt.ctx+0x5f0,lo<10?(const void*)fh_cf8ec_gamma_presets[lo]:old,0x140));
        assert(!memcmp(rt.ctx+0x730,hi<10?(const void*)fh_cf8ec_gamma_presets[hi]:old+0x140,0x140));
        assert(rt.gamma_candidate_pending);
        assert(!fh_isp_runtime_apply_ced28(&rt));
        assert(!memcmp(rt.ctx+0xf20,rt.ctx+0x5f0,0x280)&&!rt.params_dirty);
    }
    /* Rejected composer input must not skip a valid independent first bank. */
    rt.ctx[0x365]=0x10;rt.ctx[0x364]=0;rt.ctx[0x36c]=0xff;rt.params_dirty=1;
    memset(rt.ctx+0x5f0,0xa6,0x140);memset(rt.ctx+0x730,0xb7,0x140);
    memcpy(old,rt.ctx+0x730,0x140);
    assert(fh_isp_runtime_apply_cfbc4(&rt)==-ERANGE);
    assert(!memcmp(rt.ctx+0x5f0,fh_cf8ec_gamma_presets[0],0x140));
    assert(!memcmp(rt.ctx+0x730,old,0x140)&&rt.gamma_candidate_pending);
    puts("Gamma unsupported preset no-op/independent banks/pending: PASS");return 0;
}
