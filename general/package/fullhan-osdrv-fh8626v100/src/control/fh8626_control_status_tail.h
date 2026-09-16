#ifndef FH8626_CONTROL_STATUS_TAIL_H
#define FH8626_CONTROL_STATUS_TAIL_H
#include <errno.h>
#include "fh8626_isp_runtime.h"
/* C9C20 writes one word; C9898 requests four words. Callers serialize this
 * with AE/AWB. A failed provider's partial output must not become state. */
typedef int (*fh_control_query_fn)(void *opaque,uint32_t *out);
static inline int fh_control_status_tail(struct fh_isp_runtime *rt,
                                         uint32_t gate_state,
                                         fh_control_query_fn mask_query,
                                         fh_control_query_fn timing_query,
                                         void *opaque)
{
    uint32_t mask,timing[4]={0};
    int first=0,rc;
    if(!rt)return -EINVAL;
    mask=rt->ae_state.dirty_mask;
    rc=mask_query?mask_query(opaque,&mask):-ENOSYS;
    if(rc){first=rc;mask=rt->ae_state.dirty_mask;}
    /* C949C does not branch on C9C20's return: independent current slots
     * must advance even when the driver mask could not be refreshed. */
    rc=fh_isp_runtime_c949c_publish_tail(rt,mask,gate_state);
    if(rc&&!first)first=rc;
    rc=timing_query?timing_query(opaque,timing):-ENOSYS;
    if(rc){if(!first)first=rc;timing[0]=rt->ae_state.slot[0];}
    /* Preserve unknown timing rather than vendor uninitialized stack bytes,
     * but still mirror slots1..6 and validate diagnostics7/8. */
    rc=fh_isp_runtime_c9898_ingest_stat0(rt,timing[0]);
    if(rc&&!first)first=rc;
    rt->last_control_tail_error=first;
    return first;
}
#endif
