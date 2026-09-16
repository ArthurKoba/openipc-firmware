#ifndef FH8626_OWNER_NR3D_LIFECYCLE_H
#define FH8626_OWNER_NR3D_LIFECYCLE_H
#include <errno.h>
#include <string.h>
#include "fh8626_isp_runtime.h"
#include "../media_contracts/nr3d/fh8626_nr3d_kernel.h"
/* C1EE4 passes ctx11AC to request80206926 (32-byte output). Only word0
 * is interpreted here; the remaining driver words must not be invented. */
typedef int (*fh_nr3d_query_fn)(void *opaque,struct fh_nr3d_driver_config *out);
static inline int fh_nr3d_read_driver_config(struct fh_isp_runtime *rt,
                                             fh_nr3d_query_fn query,void *opaque)
{
    struct fh_nr3d_driver_config cfg={0};
    int rc;
    if(!rt||!query)return -EINVAL;
    rc=query(opaque,&cfg);
    /* A failing provider may have partially modified its output. Invalidate
     * local eligibility instead of interpreting those bytes as mode1. */
    if(rc)memset(rt->ctx+0x11ac,0,sizeof(cfg));
    else memcpy(rt->ctx+0x11ac,&cfg,sizeof(cfg));
    return rc;
}
typedef int (*fh_nr3d_kernel_fn)(void *opaque,int enabled);
/* One owner policy for boot opt-in and manual control. A successful proc
 * write is not hardware readback or proof that stock temporal buffers reset.
 * This fence protects our gain/history publication, not invented buffers. */
static inline int fh_nr3d_request(struct fh_isp_runtime *rt,int *disabled,
                                  int *pending,int enabled,
                                  fh_nr3d_kernel_fn kernel,void *opaque)
{
    int rc;
    if(!rt||!disabled||!pending||!kernel)return -EINVAL;
    rc=kernel(opaque,0);
    if(rc)return rc;
    fh_isp_runtime_set_nr3d_enabled(rt,enabled);
    *disabled=!enabled;*pending=!!enabled;
    if(enabled)fh_isp_runtime_prepare_nr3d_reenable(rt);
    return 0;
}
static inline int fh_nr3d_finish_reenable(struct fh_isp_runtime *rt,int *pending,
                                          fh_nr3d_kernel_fn kernel,void *opaque)
{
    int rc;
    if(!rt||!pending||!kernel)return -EINVAL;
    if(!*pending||!fh_isp_runtime_nr3d_ready(rt))return 0;
    rc=kernel(opaque,1);
    if(!rc)*pending=0;
    return rc;
}
#endif
