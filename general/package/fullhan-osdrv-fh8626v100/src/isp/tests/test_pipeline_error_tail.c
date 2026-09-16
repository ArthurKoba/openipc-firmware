#include "fh8626_isp_runtime.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
static unsigned ae_calls,awb_calls;
static int ae(void *p){(void)p;ae_calls++;return 0;}
static int awb(void *p){(void)p;awb_calls++;return -ECANCELED;}
int main(void)
{
    struct fh_isp_runtime rt;
    uint32_t regs[0x1000]={0},v;
    fh_isp_runtime_reset(&rt);
    assert(!fh_isp_runtime_attach_mmio(&rt,regs,sizeof(regs)));
    rt.stock_runtime_started=1;
    rt.ctx[0x10]=1; /* AE enabled, no mapped C6C00 provider: do not actuate. */
    rt.ctx[0x11]=1; /* BLC requires initialized total gain: local EAGAIN. */
    rt.ctx[0x13]=0x50; /* invalid purple angle, then independent FC writer. */
    rt.ctx[0x978]=0xff;rt.ctx[0x979]=1;
    rt.ctx[0x25c]=0xab;
    regs[0x84/4]=0x11223344;regs[0x59c/4]=0x55667788;
    regs[0x4b8/4]=0x12345678;
    rt.gamma_candidate_pending=1;rt.params_dirty=1;
    memset(rt.ctx+0x5f0,0x51,0x280);
    for(unsigned i=0;i<2;i++){
        assert(!fh_isp_runtime_tick_with_control_hooks(&rt,ae,NULL,awb,NULL));
        assert(ae_calls==0&&awb_calls==i+1);
        assert(rt.last_ae_error==-ENODATA&&rt.last_awb_error==-ECANCELED);
        assert(rt.last_stage_error==-EAGAIN&&rt.last_stage_address==0xce430);
        assert(rt.stage_error_count==2);
        assert(regs[0x84/4]==0x11223344&&regs[0x59c/4]==0x55667788);
        assert(regs[0x4b8/4]==0xab345678);
        assert(!memcmp(rt.ctx+0xf20,rt.ctx+0x5f0,0x280));
        assert(!rt.params_dirty&&!rt.gamma_candidate_pending);
    }
    rt.ctx[0x10]=0;rt.ctx[0x11]=0;rt.ctx[0x13]=0x40;
    assert(!fh_isp_runtime_tick_proven_subset(&rt));
    assert(!rt.last_ae_error&&!rt.last_awb_error&&!rt.last_stage_error);
    assert(!rt.stage_error_count&&!rt.last_stage_address);
    /* CED28's UINT_MAX is a result, not a fatal control error. */
    rt.params_dirty=1;
    assert(!fh_isp_runtime_tick_proven_subset(&rt));
    memcpy(&v,rt.ctx+0x11a8,4);assert(v==UINT32_MAX&&rt.params_dirty);
    assert(!rt.stage_error_count);
    assert(fh_isp_runtime_tick_proven_subset(NULL)==-EINVAL);
    puts("ISP rejected inputs/local errors/independent tail: PASS");
    return 0;
}
