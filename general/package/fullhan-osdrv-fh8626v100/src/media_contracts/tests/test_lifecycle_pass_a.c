#define _GNU_SOURCE
#include "fh8626_bgm_adapter.h"
#include "fh8626_bgm_stock_route.h"
#include "fh8626_ghosting_policy.h"
#include "fh8626_h264_control.h"
#include "fh8626_nr3d_lifecycle.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Independent contract pass A: native BGM + NR3D cold epoch, unknown unbind
 * completion, explicit recovery, and a second cold epoch. */
struct bgm_fake {
    uint32_t query_bytes;
    unsigned releases;
    unsigned reopens;
};

static int b_alloc(void *o, uint32_t bytes, uint32_t align, struct fh_bgm_block *b)
{
    (void)o;
    assert(align == 0x400u);
    b->phys = 0x100000u;
    b->user = (uintptr_t)0x200000u;
    b->bytes = bytes;
    return 0;
}
static int b_free(void *o, struct fh_bgm_block *b)
{
    (void)o;
    memset(b, 0, sizeof(*b));
    return 0;
}
static int b_io(void *o, unsigned long req, void *arg, uint32_t *raw)
{
    struct bgm_fake *f = o;
    if (raw) *raw = 0;
    if (req == FH_BGM_MEM_QUERY) {
        struct fh_bgm_mem_query *q = arg;
        q->bytes = f->query_bytes ? f->query_bytes : 581688u;
    }
    return 0;
}
static int b_release(void *o, struct fh_bgm_block *b)
{
    struct bgm_fake *f = o;
    f->releases++;
    memset(b, 0, sizeof(*b));
    return 0;
}
static int b_reopen(void *o)
{
    ((struct bgm_fake *)o)->reopens++;
    return 0;
}
static int b_needs(void *o)
{
    (void)o;
    return 0;
}

struct route_fake {
    char readback[512];
    unsigned writes;
    unsigned ioctls;
    unsigned fail_ioctl_at;
};
static int r_write(void *o, const char *path, const char *text, size_t len)
{
    struct route_fake *f = o;
    assert(!strcmp(path, FH_ENC_PROC_PATH));
    assert(len > 0 && text[len - 1] == '\n');
    f->writes++;
    return 0;
}
static int r_read(void *o, const char *path, char *buf, size_t cap, size_t *used)
{
    struct route_fake *f = o;
    size_t n = strlen(f->readback);
    assert(!strcmp(path, FH_ENC_PROC_PATH));
    assert(n < cap);
    memcpy(buf, f->readback, n);
    *used = n;
    return 0;
}
static int r_ioctl(void *o, unsigned long req, void *arg, size_t size)
{
    struct route_fake *f = o;
    f->ioctls++;
    if (req == FH_MEDIA_BIND) {
        const struct fh_media_bind_wire *w = arg;
        assert(size == sizeof(*w) && w->source == 5 && w->destination == 17);
    } else if (req == FH_MEDIA_UNBIND_SRC) {
        const struct fh_media_unbind_src_wire *w = arg;
        assert(size == sizeof(*w) && w->source == 5);
    } else {
        assert(0);
    }
    if (f->fail_ioctl_at && f->ioctls == f->fail_ioctl_at)
        return -EIO;
    return 0;
}

struct nr_fake { uint32_t mode; };
static int nr_ioctl(void *o, unsigned long req, void *arg)
{
    struct nr_fake *f = o;
    assert(req == FH_ISP_NR3D_QUERY);
    ((struct fh_nr3d_driver_config *)arg)->mode = f->mode;
    return 0;
}

struct h_fake { unsigned long last; };
static int h_ioctl(void *o, unsigned long req, void *arg)
{
    struct h_fake *f = o;
    (void)arg;
    f->last = req;
    return 0;
}

static void set_bgm_readback(struct route_fake *r, int on)
{
    snprintf(r->readback, sizeof(r->readback),
             "****<Channel-0 RUN>****\n## Function:\n%s\n",
             on ? "[TEXTURE][BGM]" : "");
}

int main(void)
{
    char nr_path[] = "/tmp/fh_lifecycle_a_nr3d_XXXXXX";
    int fd = mkstemp(nr_path);
    struct bgm_fake bf = {581688u, 0, 0};
    struct fh_bgm_ops bops = {b_alloc, b_free, b_io, b_release, b_reopen, b_needs, &bf};
    struct fh_bgm_adapter bgm;
    struct route_fake rf;
    struct fh_bgm_route_ops rops;
    struct fh_bgm_stock_route route;
    struct nr_fake nf = {1};
    struct fh_nr3d_kernel nk = {nr_ioctl, &nf, nr_path};
    struct fh_nr3d_lifecycle nr;
    struct fh_ghosting_policy policy;
    struct h_fake hf = {0};
    struct fh_h264_control hc = {h_ioctl, &hf, 0};
    struct fh_pae_rc_realtime_wire rt = {0, 640000u, (100u << 16) | 500u,
                                         25u, 50u, 28u, 50u};

    assert(fd >= 0);
    close(fd);
    memset(&rf, 0, sizeof(rf));
    rops.write_text = r_write;
    rops.read_text = r_read;
    rops.media_ioctl = r_ioctl;
    rops.opaque = &rf;

    /* init */
    assert(!fh_bgm_adapter_init(&bgm, &bops));
    assert(!fh_bgm_stock_route_init(&route, &rops, 0));
    assert(!fh_nr3d_lifecycle_init(&nr, &nk));
    fh_ghosting_policy_defaults(&policy);
    policy.bgm_mode = FH_BGM_MODE_ENCODER_NATIVE;
    policy.nr3d_enabled = 1;

    /* run: capability prepare -> external cold PAE_SET_CONFIG -> readback ->
     * BGM start/bind; NR3D is selected during the same cold epoch. */
    assert(!fh_bgm_stock_prepare_encoder_capability(&route, 1, 1));
    set_bgm_readback(&rf, 1);
    assert(!fh_bgm_stock_verify_encoder_capability(&route));
    assert(!fh_bgm_adapter_start(&bgm, 160, 90, FH_BGM_MODE_ENCODER_NATIVE));
    assert(!fh_bgm_stock_bind(&route));
    nf.mode = 1;
    assert(!fh_nr3d_lifecycle_cold_start(&nr, 1));
    assert(!fh_h264_change_rc_realtime(&hc, &rt));
    assert(hf.last == FH_PAE_CHANGE_RC_REALTIME);

    /* error: exact source-unbind ioctl fails -> completion unknown. */
    rf.fail_ioctl_at = rf.ioctls + 1;
    assert(fh_bgm_stock_unbind(&route) == -EIO);
    assert(fh_bgm_stock_route_needs_recovery(&route));

    /* stop: BGM native stop holds memory; NR3D selector can go OFF but history
     * is not claimed drained. */
    assert(!fh_bgm_adapter_stop(&bgm));
    assert(bgm.state == FH_BGM_HELD);
    nf.mode = 0;
    assert(!fh_nr3d_lifecycle_hot_disable(&nr));
    assert(nr.state == FH_NR3D_HOT_DISABLED);

    /* mode-change + sensor-change while unknown/held -> recovery-required. */
    assert(fh_ghosting_policy_decide_change(&policy, FH_GHOSTING_CHANGE_MODE,
            bgm.state, route.state, nr.state) == FH_GHOSTING_RECOVERY_REQUIRED);
    assert(fh_ghosting_policy_decide_change(&policy, FH_GHOSTING_CHANGE_SENSOR,
            bgm.state, route.state, nr.state) == FH_GHOSTING_RECOVERY_REQUIRED);

    /* restart: caller has now performed the external media/ISP restart boundary. */
    rf.fail_ioctl_at = 0;
    assert(!fh_bgm_stock_route_begin_after_external_recovery(&route));
    assert(!fh_bgm_adapter_recover(&bgm));
    assert(!fh_nr3d_lifecycle_begin_after_external_restart(&nr));
    assert(bf.releases == 1 && bf.reopens == 1);

    /* second cold run proves no hidden HELD/HOT_DISABLED reuse. */
    assert(!fh_bgm_stock_prepare_encoder_capability(&route, 1, 1));
    set_bgm_readback(&rf, 1);
    assert(!fh_bgm_stock_verify_encoder_capability(&route));
    assert(!fh_bgm_adapter_start(&bgm, 160, 90, FH_BGM_MODE_ENCODER_NATIVE));
    assert(!fh_bgm_stock_bind(&route));
    nf.mode = 1;
    assert(!fh_nr3d_lifecycle_cold_start(&nr, 1));

    /* A normal mode/sensor change is still a restart boundary with active
     * histories even though no subsystem is poisoned. */
    assert(fh_ghosting_policy_decide_change(&policy, FH_GHOSTING_CHANGE_MODE,
            bgm.state, route.state, nr.state) == FH_GHOSTING_RESTART_REQUIRED);
    assert(fh_ghosting_policy_decide_change(&policy, FH_GHOSTING_CHANGE_SENSOR,
            bgm.state, route.state, nr.state) == FH_GHOSTING_RESTART_REQUIRED);

    assert(!fh_bgm_stock_unbind(&route));
    assert(!fh_bgm_adapter_stop(&bgm));
    nf.mode = 0;
    assert(!fh_nr3d_lifecycle_hot_disable(&nr));
    assert(!fh_bgm_adapter_recover(&bgm));
    assert(!fh_bgm_adapter_destroy(&bgm));
    fh_nr3d_lifecycle_destroy(&nr);
    unlink(nr_path);

    puts("Lifecycle pass A init/run/error/stop/restart/mode-change/sensor-change: PASS");
    return 0;
}
