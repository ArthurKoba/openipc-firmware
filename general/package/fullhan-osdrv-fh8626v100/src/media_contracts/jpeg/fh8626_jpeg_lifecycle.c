#include "fh8626_jpeg_lifecycle.h"

#include <errno.h>
#include <string.h>

static int valid(const struct fh_jpeg_lifecycle *lc)
{
    return lc && lc->ops && lc->ops->mem_init && lc->ops->mem_uninit &&
           lc->ops->set_config && lc->ops->start && lc->ops->stop && lc->ops->submit;
}
static int quarantine(struct fh_jpeg_lifecycle *lc, int rc)
{
    lc->state = FH_JPEG_LC_QUARANTINED;
    lc->vmm_release_ready = 0;
    return rc ? rc : -EIO;
}
void fh_jpeg_lifecycle_init(struct fh_jpeg_lifecycle *lc,
                            const struct fh_jpeg_lifecycle_ops *ops, void *opaque)
{
    if (!lc) return;
    memset(lc,0,sizeof(*lc)); lc->ops=ops; lc->opaque=opaque; lc->state=FH_JPEG_LC_EMPTY;
}
int fh_jpeg_lifecycle_mem_init(struct fh_jpeg_lifecycle *lc)
{
    int rc;
    if (!valid(lc) || lc->state != FH_JPEG_LC_EMPTY) return -EINVAL;
    if (lc->generation == UINT64_MAX) return -EOVERFLOW;
    rc=lc->ops->mem_init(lc->opaque); if(rc) return quarantine(lc,rc);
    lc->generation++; lc->state=FH_JPEG_LC_MEMORY_READY; lc->vmm_release_ready=0; return 0;
}
int fh_jpeg_lifecycle_configure(struct fh_jpeg_lifecycle *lc, uint32_t mode, int snapshot_auto_started)
{
    int rc;
    if (!valid(lc) || (mode!=1U && mode!=2U) ||
        (lc->state!=FH_JPEG_LC_MEMORY_READY && lc->state!=FH_JPEG_LC_STOPPED)) return -EINVAL;
    rc=lc->ops->set_config(lc->opaque,mode); if(rc) return quarantine(lc,rc);
    lc->mode=mode; lc->snapshot_auto_started=(mode==1U && snapshot_auto_started); lc->vmm_release_ready=0;
    lc->state=lc->snapshot_auto_started?FH_JPEG_LC_RUNNING:FH_JPEG_LC_CONFIGURED; return 0;
}
int fh_jpeg_lifecycle_apply_drop(struct fh_jpeg_lifecycle *lc)
{
    int rc;
    if (!valid(lc) || !lc->ops->set_drop || lc->mode!=2U || lc->state!=FH_JPEG_LC_CONFIGURED) return -EINVAL;
    rc=lc->ops->set_drop(lc->opaque); if(rc) return quarantine(lc,rc); return 0;
}
int fh_jpeg_lifecycle_start(struct fh_jpeg_lifecycle *lc)
{
    int rc;
    if (!valid(lc) || lc->mode!=2U || lc->state!=FH_JPEG_LC_CONFIGURED) return -EINVAL;
    rc=lc->ops->start(lc->opaque); if(rc) return quarantine(lc,rc); lc->state=FH_JPEG_LC_RUNNING; return 0;
}
int fh_jpeg_lifecycle_submit(struct fh_jpeg_lifecycle *lc, const struct fh_jpeg_yuv_submit_wire *wire,
                             uint64_t *input_generation)
{
    struct fh_jpeg_yuv_submit_wire tmp; uint64_t pts; int rc;
    if (!valid(lc) || !wire || lc->state!=FH_JPEG_LC_RUNNING || lc->input_held ||
        wire->mode!=lc->mode || !wire->width || !wire->height || !wire->y_phys || !wire->c_phys ||
        (wire->y_phys&3U) || (wire->c_phys&3U) || wire->input_selector!=1U) return -EINVAL;
    if (lc->input_generation==UINT64_MAX) return -EOVERFLOW;
    tmp=*wire; tmp.effective_w=wire->width; tmp.effective_h=wire->height;
    rc=lc->ops->submit(lc->opaque,&tmp); if(rc) return quarantine(lc,rc);
    pts=((uint64_t)wire->pts_hi<<32)|wire->pts_lo;
    lc->input_generation++; lc->input_pts=pts; lc->input_held=1;
    if (input_generation)
        *input_generation = lc->input_generation;
    return 0;
}
int fh_jpeg_lifecycle_observe_output_pts(struct fh_jpeg_lifecycle *lc, uint64_t pts)
{
    if (!lc || !lc->input_held) return -ENOENT;
    if (pts != lc->input_pts) return -EAGAIN;
    lc->input_held=0; return 0;
}
int fh_jpeg_lifecycle_stop(struct fh_jpeg_lifecycle *lc)
{
    int rc;
    if (!valid(lc) || lc->mode!=2U || lc->state!=FH_JPEG_LC_RUNNING || lc->snapshot_auto_started) return -EINVAL;
    rc=lc->ops->stop(lc->opaque); if(rc) return quarantine(lc,rc);
    lc->state=FH_JPEG_LC_STOPPED; return 0;
}
int fh_jpeg_lifecycle_mem_uninit(struct fh_jpeg_lifecycle *lc)
{
    int rc;
    if (!valid(lc) || lc->state!=FH_JPEG_LC_STOPPED || lc->output_held || lc->snapshot_auto_started) return -EINVAL;
    rc=lc->ops->mem_uninit(lc->opaque); if(rc) return quarantine(lc,rc);
    lc->state=FH_JPEG_LC_EMPTY; lc->mode=0; lc->input_held=0; lc->vmm_release_ready=1; return 0;
}
void fh_jpeg_lifecycle_set_output_held(struct fh_jpeg_lifecycle *lc, int held)
{ if(lc) lc->output_held=held?1:0; }
