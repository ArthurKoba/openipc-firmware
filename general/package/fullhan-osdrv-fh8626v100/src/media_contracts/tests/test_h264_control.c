#include "fh8626_h264_control.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

struct fake {
    unsigned long last;
    unsigned calls;
    int fail;
    struct fh_pae_rc_wire rc;
    struct fh_pae_rc_realtime_wire realtime;
};

static int io(void *opaque, unsigned long req, void *arg)
{
    struct fake *f = opaque;
    f->last = req;
    f->calls++;
    if (f->fail)
        return f->fail;
    if (req == FH_PAE_GET_RC_CONFIG) {
        uint32_t ch = ((struct fh_pae_rc_wire *)arg)->chn;
        *(struct fh_pae_rc_wire *)arg = f->rc;
        ((struct fh_pae_rc_wire *)arg)->chn = ch;
    } else if (req == FH_PAE_SET_RC_CONFIG) {
        f->rc = *(struct fh_pae_rc_wire *)arg;
    } else if (req == FH_PAE_CHANGE_RC_REALTIME) {
        f->realtime = *(struct fh_pae_rc_realtime_wire *)arg;
    }
    return 0;
}

static uint32_t fps(uint16_t low, uint16_t high)
{
    return (uint32_t)low | ((uint32_t)high << 16);
}

static void fill_common_rc(struct fh_pae_rc_wire *rc, uint32_t mode)
{
    memset(rc, 0, sizeof(*rc));
    rc->chn = 0;
    rc->rc_mode = mode;
    rc->frame_rate_packed = fps(1666, 100);
    rc->init_qp = 38;
    rc->bitrate_or_rate = 800000;
    rc->i_min_qp = 25;
    rc->i_max_qp = 50;
    rc->p_min_qp = 28;
    rc->p_max_qp = 50;
    rc->i_proportion = 5;
    rc->p_proportion = 1;
    rc->fluctuate_level = 0;
    rc->ip_qp_delta = 0;
    rc->i_target_limit_bits = 0;
    rc->max_rate_percent = 120;
    rc->still_rate_percent = 30;
    rc->max_still_qp = 38;
    rc->additional_rate_bits = 0;
    rc->extra_qp_parameter = 0;
}

int main(void)
{
    struct fake f;
    struct fh_h264_control c;
    struct fh_pae_rc_wire full;
    struct fh_pae_rc_realtime_wire rt;
    unsigned calls;

    memset(&f, 0, sizeof(f));
    c.ioctl = io;
    c.opaque = &f;
    c.channel = 0;
    fill_common_rc(&f.rc, FH_PAE_RC_CBR);

    /* Force-I is independent from RC restart. */
    assert(fh_h264_force_i(&c) == 0);
    assert(f.last == FH_PAE_FORCE_I);

    /* Full RC preserves the exact 0x54 layout including corrected 0x3c..0x50 semantics. */
    full = f.rc;
    full.i_target_limit_bits = 0x11111111u;
    full.max_rate_percent = 120;
    full.still_rate_percent = 30;
    full.max_still_qp = 38;
    full.additional_rate_bits = 0x22222222u;
    full.extra_qp_parameter = 0x33333333u;
    assert(fh_h264_set_rc_cold(&c, &full) == 0);
    assert(f.last == FH_PAE_SET_RC_CONFIG);
    assert(f.rc.i_target_limit_bits == 0x11111111u);
    assert(f.rc.max_rate_percent == 120 && f.rc.still_rate_percent == 30);
    assert(f.rc.max_still_qp == 38 && f.rc.additional_rate_bits == 0x22222222u);
    assert(f.rc.extra_qp_parameter == 0x33333333u);

    /* Numeric wire modes are 0..5. Mode 3 is accepted but semantically unresolved. */
    full = f.rc;
    full.rc_mode = 6;
    calls = f.calls;
    assert(fh_h264_set_rc_cold(&c, &full) == -EINVAL);
    assert(f.calls == calls); /* invalid input rejected before any ioctl */

    fill_common_rc(&full, FH_PAE_RC_MODE3_UNRESOLVED);
    assert(fh_h264_set_rc_cold(&c, &full) == 0);
    assert(f.rc.rc_mode == FH_PAE_RC_MODE3_UNRESOLVED);

    /* Common native-RC validation reconstructed from rate_control_init(). */
    fill_common_rc(&full, FH_PAE_RC_CBR);
    full.i_proportion = 0;
    calls = f.calls;
    assert(fh_h264_set_rc_cold(&c, &full) == -EINVAL);
    assert(f.calls == calls);

    fill_common_rc(&full, FH_PAE_RC_CBR);
    full.max_rate_percent = 109;
    calls = f.calls;
    assert(fh_h264_set_rc_cold(&c, &full) == -EINVAL);
    assert(f.calls == calls);

    fill_common_rc(&full, FH_PAE_RC_AVBR);
    full.still_rate_percent = 24;
    calls = f.calls;
    assert(fh_h264_set_rc_cold(&c, &full) == -EINVAL);
    assert(f.calls == calls);

    fill_common_rc(&full, FH_PAE_RC_AVBR);
    full.max_still_qp = 52;
    calls = f.calls;
    assert(fh_h264_set_rc_cold(&c, &full) == -EINVAL);
    assert(f.calls == calls);

    /* Fixed-QP mode takes the driver's early branch after FPS validation. */
    memset(&full, 0, sizeof(full));
    full.chn = 0;
    full.rc_mode = FH_PAE_RC_FIXED_QP;
    full.frame_rate_packed = fps(1666, 100);
    full.mode2_qp_a = 30;
    full.mode2_qp_b = 31;
    full.init_qp = 0xffffffffu;       /* not checked on the fixed-QP branch */
    full.max_rate_percent = 0;        /* not checked on the fixed-QP branch */
    assert(fh_h264_set_rc_cold(&c, &full) == 0);
    full.mode2_qp_b = 52;
    calls = f.calls;
    assert(fh_h264_set_rc_cold(&c, &full) == -EINVAL);
    assert(f.calls == calls);

    /* Driver validation checks each min/max QP bound independently; the stricter
     * min<=max relation belongs only to the optional SDK policy helper. */
    fill_common_rc(&full, FH_PAE_RC_CBR);
    full.i_min_qp = 50;
    full.i_max_qp = 49;
    assert(fh_pae_rc_validate_driver(&full) == 0);
    assert(fh_pae_rc_validate_sdk_policy(&full) == -1);
    assert(fh_h264_set_rc_cold(&c, &full) == 0);

    /* Exact realtime path: seven words, no RC-mode mutation. */
    memset(&rt, 0, sizeof(rt));
    rt.chn = 0;
    rt.rate = 640000;
    rt.frame_rate_packed = fps(500, 100);
    rt.i_min_qp = 25;
    rt.i_max_qp = 49;
    rt.p_min_qp = 28;
    rt.p_max_qp = 50;
    assert(fh_h264_change_rc_realtime(&c, &rt) == 0);
    assert(f.last == FH_PAE_CHANGE_RC_REALTIME);
    assert(!memcmp(&f.realtime, &rt, sizeof(rt)));
    assert(f.rc.rc_mode == FH_PAE_RC_CBR);

    calls = f.calls;
    rt.chn = 1;
    assert(fh_h264_change_rc_realtime(&c, &rt) == -EINVAL);
    assert(f.calls == calls);
    rt.chn = 0;

    c.channel = 8;
    assert(fh_h264_change_rc_realtime(&c, &rt) == -EINVAL);
    assert(fh_h264_force_i(&c) == -EINVAL);
    c.channel = 0;

    rt.frame_rate_packed = fps(0, 100);
    assert(fh_h264_change_rc_realtime(&c, &rt) == -EINVAL);
    rt.frame_rate_packed = fps(500, 0);
    assert(fh_h264_change_rc_realtime(&c, &rt) == -EINVAL);
    rt.frame_rate_packed = fps(500, 100);

    rt.i_max_qp = 52;
    assert(fh_h264_change_rc_realtime(&c, &rt) == -EINVAL);
    rt.i_max_qp = 49;
    rt.i_min_qp = 50; /* reversed but individually within driver bounds */
    assert(fh_h264_change_rc_realtime(&c, &rt) == 0);
    rt.i_min_qp = 25;
    rt.p_min_qp = 51;
    rt.p_max_qp = 50;
    assert(fh_h264_change_rc_realtime(&c, &rt) == 0);
    rt.p_min_qp = 28;

    /* No invented rate restriction: zero is passed through to the driver. */
    rt.rate = 0;
    assert(fh_h264_change_rc_realtime(&c, &rt) == 0);
    assert(f.realtime.rate == 0);


    /* PAE receive start/stop are exact channel ioctls, not full owner restart. */
    calls = f.calls;
    assert(fh_h264_start_recv(&c) == 0 && f.last == FH_PAE_START_RECV && f.calls == calls + 1);
    assert(fh_h264_stop_recv(&c) == 0 && f.last == FH_PAE_STOP_RECV && f.calls == calls + 2);

    /* Stock bitrate helper is safe realtime only for VBR/AVBR. */
    assert(fh_h264_bitrate_apply_path(FH_APOLLO_RC_VBR) == FH_H264_BITRATE_REALTIME);
    assert(fh_h264_bitrate_apply_path(FH_APOLLO_RC_AVBR) == FH_H264_BITRATE_REALTIME);
    assert(fh_h264_bitrate_apply_path(FH_APOLLO_RC_CBR) == FH_H264_BITRATE_RESTART_REQUIRED);
    assert(fh_h264_bitrate_apply_path(FH_APOLLO_RC_CVBR) == FH_H264_BITRATE_RESTART_REQUIRED);
    fill_common_rc(&full, FH_PAE_RC_VBR);
    memset(&rt, 0xa5, sizeof(rt));
    assert(fh_h264_build_realtime_bitrate(&full, 123456, &rt) == 0);
    assert(rt.chn == 0 && rt.rate == 123456 && rt.i_max_qp == full.i_max_qp);
    fill_common_rc(&full, FH_PAE_RC_CVBR);
    memset(&rt, 0x5a, sizeof(rt));
    { struct fh_pae_rc_realtime_wire before = rt;
      assert(fh_h264_build_realtime_bitrate(&full, 123456, &rt) == -ENOTSUP);
      assert(!memcmp(&rt, &before, sizeof(rt))); }

    puts("H264 cold/full + realtime RC exact ABI/driver-vs-policy validation: PASS");
    return 0;
}
