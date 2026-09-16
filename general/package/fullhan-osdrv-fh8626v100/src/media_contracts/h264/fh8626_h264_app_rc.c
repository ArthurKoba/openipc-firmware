#include "fh8626_h264_app_rc.h"

#include <errno.h>
#include <string.h>

int fh_h264_public_rc_parse(const char *name, uint32_t *public_mode)
{
    static const char *const names[] = {"cbr","vbr","avbr","cvbr","qvbr"};
    uint32_t i;
    if (!name || !public_mode) return -EINVAL;
    for (i=0;i<5U;i++) if (strcmp(name,names[i])==0) { *public_mode=i; return 0; }
    return -EINVAL;
}

int fh_h264_apollo_rcmode_parse(const char *name, uint32_t *app_mode)
{
    if (!name || !app_mode) return -EINVAL;
    if (!strcmp(name,"vbr")) *app_mode=FH_APOLLO_RC_VBR;
    else if (!strcmp(name,"cbr")) *app_mode=FH_APOLLO_RC_CBR;
    else if (!strcmp(name,"fixeqp")) *app_mode=FH_APOLLO_RC_FIXED_QP;
    else if (!strcmp(name,"avbr")) *app_mode=FH_APOLLO_RC_AVBR;
    else if (!strcmp(name,"cvbr")) *app_mode=FH_APOLLO_RC_CVBR;
    else return -EINVAL;
    return 0;
}

int fh_h264_stock_resolve_app_mode(uint32_t public_mode, uint32_t global_app_mode, uint32_t *resolved_app_mode)
{
    if (!resolved_app_mode || public_mode > FH_PUBLIC_RC_QVBR) return -EINVAL;
    if (public_mode == FH_PUBLIC_RC_CBR) { *resolved_app_mode = FH_APOLLO_RC_CBR; return 0; }
    if (global_app_mode == FH_APOLLO_RC_VBR || global_app_mode == FH_APOLLO_RC_CVBR)
        *resolved_app_mode = global_app_mode;
    else
        *resolved_app_mode = FH_APOLLO_RC_AVBR;
    return 0;
}

int fh_h264_app_mode_to_wire(uint32_t app_mode, uint32_t *wire_mode)
{
    if (!wire_mode)
        return -EINVAL;
    switch (app_mode) {
    case FH_APOLLO_RC_VBR: *wire_mode = FH_PAE_RC_VBR; return 0;
    case FH_APOLLO_RC_CBR: *wire_mode = FH_PAE_RC_CBR; return 0;
    case FH_APOLLO_RC_FIXED_QP: *wire_mode = FH_PAE_RC_FIXED_QP; return 0;
    case FH_APOLLO_RC_AVBR: *wire_mode = FH_PAE_RC_AVBR; return 0;
    case FH_APOLLO_RC_CVBR: *wire_mode = FH_PAE_RC_CVBR; return 0;
    default: return -ENOTSUP;
    }
}

int fh_h264_wire_mode_to_app(uint32_t wire_mode, uint32_t *app_mode)
{
    if (!app_mode)
        return -EINVAL;
    switch (wire_mode) {
    case FH_PAE_RC_VBR: *app_mode = FH_APOLLO_RC_VBR; return 0;
    case FH_PAE_RC_CBR: *app_mode = FH_APOLLO_RC_CBR; return 0;
    case FH_PAE_RC_FIXED_QP: *app_mode = FH_APOLLO_RC_FIXED_QP; return 0;
    case FH_PAE_RC_AVBR: *app_mode = FH_APOLLO_RC_AVBR; return 0;
    case FH_PAE_RC_CVBR: *app_mode = FH_APOLLO_RC_CVBR; return 0;
    case FH_PAE_RC_MODE3_UNRESOLVED: return -ENOTSUP;
    default: return -EINVAL;
    }
}

int fh_h264_app_rc_to_wire(uint32_t channel, const struct fh_h264_app_rc *app,
                           struct fh_pae_rc_wire *wire)
{
    uint32_t mode;
    int rc;
    if (!app || !wire || !fh_pae_channel_valid(channel))
        return -EINVAL;
    rc = fh_h264_app_mode_to_wire(app->mode, &mode);
    if (rc)
        return rc;
    memset(wire, 0, sizeof(*wire));
    wire->chn = channel;
    wire->rc_mode = mode;
    wire->frame_rate_packed = app->frame_rate_packed;
    if (mode == FH_PAE_RC_FIXED_QP) {
        wire->mode2_qp_a = app->fixed_i_qp;
        wire->mode2_qp_b = app->fixed_p_qp;
        return fh_pae_rc_validate_driver(wire) ? -EINVAL : 0;
    }
    wire->init_qp = app->init_qp;
    wire->bitrate_or_rate = app->bitrate;
    wire->i_min_qp = app->i_min_qp;
    wire->i_max_qp = app->i_max_qp;
    wire->p_min_qp = app->p_min_qp;
    wire->p_max_qp = app->p_max_qp;
    wire->i_proportion = app->i_proportion;
    wire->p_proportion = app->p_proportion;
    wire->fluctuate_level = app->fluctuate_level;
    wire->ip_qp_delta = app->ip_qp_delta;
    wire->i_target_limit_bits = app->i_target_limit_bits;
    wire->max_rate_percent = app->max_rate_percent;
    wire->still_rate_percent = app->still_rate_percent;
    wire->max_still_qp = app->max_still_qp;
    wire->additional_rate_bits = app->secondary_rate;
    wire->extra_qp_parameter = app->extra_qp_parameter;
    return fh_pae_rc_validate_driver(wire) ? -EINVAL : 0;
}

int fh_h264_wire_rc_to_app(const struct fh_pae_rc_wire *wire,
                           struct fh_h264_app_rc *app)
{
    uint32_t mode;
    int rc;
    if (!wire || !app)
        return -EINVAL;
    rc = fh_h264_wire_mode_to_app(wire->rc_mode, &mode);
    if (rc)
        return rc;
    memset(app, 0, sizeof(*app));
    app->mode = mode;
    app->frame_rate_packed = wire->frame_rate_packed;
    if (wire->rc_mode == FH_PAE_RC_FIXED_QP) {
        app->fixed_i_qp = wire->mode2_qp_a;
        app->fixed_p_qp = wire->mode2_qp_b;
        return 0;
    }
    app->init_qp = wire->init_qp;
    app->bitrate = wire->bitrate_or_rate;
    app->i_min_qp = wire->i_min_qp;
    app->i_max_qp = wire->i_max_qp;
    app->p_min_qp = wire->p_min_qp;
    app->p_max_qp = wire->p_max_qp;
    app->i_proportion = wire->i_proportion;
    app->p_proportion = wire->p_proportion;
    app->fluctuate_level = wire->fluctuate_level;
    app->ip_qp_delta = wire->ip_qp_delta;
    app->i_target_limit_bits = wire->i_target_limit_bits;
    app->max_rate_percent = wire->max_rate_percent;
    app->still_rate_percent = wire->still_rate_percent;
    app->max_still_qp = wire->max_still_qp;
    app->secondary_rate = wire->additional_rate_bits;
    app->extra_qp_parameter = wire->extra_qp_parameter;
    return 0;
}

int fh_h264_app_rc_set_idr_qp(struct fh_h264_app_rc *app, uint32_t requested_qp)
{
    uint32_t limit;
    if (!app)
        return -EINVAL;
    switch (app->mode) {
    case FH_APOLLO_RC_VBR:
    case FH_APOLLO_RC_AVBR:
        limit = app->i_max_qp;
        break;
    case FH_APOLLO_RC_CVBR:
        limit = app->i_max_qp;
        break;
    case FH_APOLLO_RC_CBR:
        limit = FH_PAE_MAX_QP;
        break;
    case FH_APOLLO_RC_FIXED_QP:
        return 0;
    default:
        return -ENOTSUP;
    }
    if (limit > FH_PAE_MAX_QP)
        limit = FH_PAE_MAX_QP;
    app->init_qp = requested_qp > limit ? limit : requested_qp;
    return 0;
}
