#include "fh8626_h264_control.h"

#include <errno.h>
#include <string.h>

static int fh_h264_control_valid(const struct fh_h264_control *c)
{
    return c && c->ioctl && fh_pae_channel_valid(c->channel);
}

static int channel_ioctl(struct fh_h264_control *c, unsigned long req)
{
    uint32_t ch;
    if (!fh_h264_control_valid(c))
        return -EINVAL;
    ch = c->channel;
    return c->ioctl(c->opaque, req, &ch);
}

int fh_h264_start_recv(struct fh_h264_control *c)
{
    return channel_ioctl(c, FH_PAE_START_RECV);
}

int fh_h264_stop_recv(struct fh_h264_control *c)
{
    return channel_ioctl(c, FH_PAE_STOP_RECV);
}

int fh_h264_force_i(struct fh_h264_control *c)
{
    return channel_ioctl(c, FH_PAE_FORCE_I);
}

int fh_h264_get_rc(struct fh_h264_control *c, struct fh_pae_rc_wire *out)
{
    if (!fh_h264_control_valid(c) || !out)
        return -EINVAL;
    memset(out, 0, sizeof(*out));
    out->chn = c->channel;
    return c->ioctl(c->opaque, FH_PAE_GET_RC_CONFIG, out);
}

int fh_h264_set_rc_cold(struct fh_h264_control *c, const struct fh_pae_rc_wire *next)
{
    struct fh_pae_rc_wire copy;
    if (!fh_h264_control_valid(c) || !next || next->chn != c->channel)
        return -EINVAL;
    if (fh_pae_rc_validate_driver(next))
        return -EINVAL;
    copy = *next;
    return c->ioctl(c->opaque, FH_PAE_SET_RC_CONFIG, &copy);
}

int fh_h264_change_rc_realtime(struct fh_h264_control *c,
                               const struct fh_pae_rc_realtime_wire *next)
{
    struct fh_pae_rc_realtime_wire copy;
    if (!fh_h264_control_valid(c) || !next || next->chn != c->channel)
        return -EINVAL;
    if (fh_pae_rc_realtime_validate_driver(next))
        return -EINVAL;
    copy = *next;
    return c->ioctl(c->opaque, FH_PAE_CHANGE_RC_REALTIME, &copy);
}

int fh_h264_bitrate_apply_path(uint32_t app_mode)
{
    switch (app_mode) {
    case FH_APOLLO_RC_VBR:
    case FH_APOLLO_RC_AVBR:
        return FH_H264_BITRATE_REALTIME;
    case FH_APOLLO_RC_CBR:
    case FH_APOLLO_RC_FIXED_QP:
    case FH_APOLLO_RC_CVBR:
        return FH_H264_BITRATE_RESTART_REQUIRED;
    default:
        return -EINVAL;
    }
}

int fh_h264_build_realtime_bitrate(const struct fh_pae_rc_wire *current,
                                   uint32_t new_rate,
                                   struct fh_pae_rc_realtime_wire *out)
{
    struct fh_pae_rc_realtime_wire tmp;
    if (!current || !out)
        return -EINVAL;
    if (current->rc_mode != FH_PAE_RC_VBR && current->rc_mode != FH_PAE_RC_AVBR)
        return -ENOTSUP;
    if (fh_pae_rc_validate_driver(current))
        return -EINVAL;
    memset(&tmp, 0, sizeof(tmp));
    tmp.chn = current->chn;
    tmp.rate = new_rate;
    tmp.frame_rate_packed = current->frame_rate_packed;
    tmp.i_min_qp = current->i_min_qp;
    tmp.i_max_qp = current->i_max_qp;
    tmp.p_min_qp = current->p_min_qp;
    tmp.p_max_qp = current->p_max_qp;
    if (fh_pae_rc_realtime_validate_driver(&tmp))
        return -EINVAL;
    *out = tmp;
    return 0;
}
