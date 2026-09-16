#include "fh8626_nr3d_lifecycle.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
struct probe {int fail;unsigned calls;int last;};
struct query_probe {int fail;struct fh_nr3d_driver_config config;};
static int query_config(void *opaque,struct fh_nr3d_driver_config *out)
{
    struct query_probe *p=opaque;*out=p->config;return p->fail;
}
static int kernel(void *opaque,int on)
{
    struct probe *p=opaque;p->calls++;p->last=on;return p->fail;
}
int main(void)
{
    struct fh_isp_runtime rt,before;
    struct probe p={0};
    uint32_t regs[0x1000]={0};
    int disabled=1,pending=0;
    fh_isp_runtime_reset(&rt);
    {
        struct query_probe qp={0,{1,{11,22,33,44,55,66,77}}};
        rt.ctx[0x11ab]=0x5a;rt.ctx[0x11cc]=0xa5;
        assert(!fh_nr3d_read_driver_config(&rt,query_config,&qp));
        assert(!memcmp(rt.ctx+0x11ac,&qp.config,32));
        qp.fail=-EIO;
        assert(fh_nr3d_read_driver_config(&rt,query_config,&qp)==-EIO);
        for(unsigned i=0;i<32;i++)assert(rt.ctx[0x11ac+i]==0);
        assert(rt.ctx[0x11ab]==0x5a&&rt.ctx[0x11cc]==0xa5);
        assert(fh_nr3d_read_driver_config(NULL,query_config,&qp)==-EINVAL);
        assert(fh_nr3d_read_driver_config(&rt,NULL,&qp)==-EINVAL);
    }
    assert(!fh_isp_runtime_nr3d_ready(&rt)); /* equal zero epochs are not ready */
    assert(!fh_isp_runtime_attach_mmio(&rt,regs,sizeof(regs)));
    rt.profile_generation=4;rt.ctx[0x3a]=2;rt.ctx[0xa70]=1;
    rt.exposure_history[0]=771;rt.gain_history[0]=99;rt.control_delta_history[59]=31;
    rt.ctx[0x11]=3;rt.source_stats_epoch=17;
    before=rt;p.fail=-EIO;
    assert(fh_nr3d_request(&rt,&disabled,&pending,1,kernel,&p)==-EIO);
    assert(!memcmp(&rt,&before,sizeof(rt))&&disabled==1&&!pending&&p.last==0);
    p.fail=0;
    assert(!fh_nr3d_request(&rt,&disabled,&pending,1,kernel,&p));
    assert(!disabled&&pending&&rt.nr3d_enabled&&rt.nr3d_warmup_left==3);
    assert(rt.ctx[0x11]==0x43&&rt.exposure_history[0]==771&&rt.gain_history[0]==99);
    assert(rt.control_delta_history[59]==31&&rt.nr3d_warmup_epoch==17);
    assert(!fh_nr3d_finish_reenable(&rt,&pending,kernel,&p)&&p.calls==2);
    /* Replaying the epoch on which opt-in occurred does not satisfy a fence. */
    assert(!fh_isp_runtime_apply_c73f8(&rt)&&rt.nr3d_warmup_left==3);
    for(unsigned epoch=18;epoch<=20;epoch++){
        fh_isp_runtime_accept_stats_epoch(&rt,epoch);
        assert(!fh_isp_runtime_apply_c73f8(&rt));
        assert(rt.nr3d_warmup_left==20-epoch);
        assert(!fh_isp_runtime_apply_c73f8(&rt));
        assert(rt.nr3d_warmup_left==20-epoch);
    }
    /* Fresh history alone must not enable an unconfigured driver path. */
    assert(!fh_isp_runtime_nr3d_ready(&rt));
    assert(!fh_nr3d_finish_reenable(&rt,&pending,kernel,&p)&&p.calls==2&&pending);
    for(uint32_t mode=2;mode<=3;mode++){
        memcpy(rt.ctx+0x11ac,&mode,4);
        assert(!fh_isp_runtime_nr3d_ready(&rt));
    }
    { uint32_t mode=1;memcpy(rt.ctx+0x11ac,&mode,4); }
    assert(fh_isp_runtime_nr3d_ready(&rt));
    rt.ctx[0xa70]=0;assert(!fh_isp_runtime_nr3d_ready(&rt));rt.ctx[0xa70]=1;
    fh_isp_runtime_accept_stats_epoch(&rt,21);assert(!fh_isp_runtime_nr3d_ready(&rt));
    assert(!fh_isp_runtime_apply_c73f8(&rt)&&fh_isp_runtime_nr3d_ready(&rt));
    p.fail=-EIO;
    assert(fh_nr3d_finish_reenable(&rt,&pending,kernel,&p)==-EIO&&pending&&p.last==1);
    p.fail=0;
    assert(!fh_nr3d_finish_reenable(&rt,&pending,kernel,&p)&&!pending);
    assert(!fh_nr3d_request(&rt,&disabled,&pending,0,kernel,&p));
    assert(disabled&&!pending&&rt.ctx[0x11]==3&&!fh_isp_runtime_nr3d_ready(&rt));
    /* Same request handles boot/manual opt-in; no immediate kernel-on. */
    assert(!fh_nr3d_request(&rt,&disabled,&pending,1,kernel,&p));
    assert(pending&&p.last==0&&rt.nr3d_warmup_left==3);
    puts("NR3D request/failure/fresh-epoch fence: PASS");
    return 0;
}
