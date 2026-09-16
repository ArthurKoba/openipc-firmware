#ifndef FH8626_H264_APP_RC_H
#define FH8626_H264_APP_RC_H

#include <stdint.h>
#include "fh8626_h264_rc.h"

enum fh_public_rate_control {
    FH_PUBLIC_RC_CBR = 0,
    FH_PUBLIC_RC_VBR = 1,
    FH_PUBLIC_RC_AVBR = 2,
    FH_PUBLIC_RC_CVBR = 3,
    FH_PUBLIC_RC_QVBR = 4
};

enum fh_apollo_rc_mode {
    FH_APOLLO_RC_VBR = 3,
    FH_APOLLO_RC_CBR = 4,
    FH_APOLLO_RC_FIXED_QP = 5,
    FH_APOLLO_RC_AVBR = 6,
    FH_APOLLO_RC_CVBR = 11
};

struct fh_h264_app_rc {
    uint32_t mode;
    uint32_t init_qp;
    uint32_t bitrate;
    uint32_t secondary_rate;
    uint32_t frame_rate_packed;
    uint32_t i_min_qp;
    uint32_t i_max_qp;
    uint32_t p_min_qp;
    uint32_t p_max_qp;
    uint32_t i_proportion;
    uint32_t p_proportion;
    uint32_t fluctuate_level;
    int32_t ip_qp_delta;
    uint32_t i_target_limit_bits;
    uint32_t max_rate_percent;
    uint32_t still_rate_percent;
    uint32_t max_still_qp;
    uint32_t extra_qp_parameter;
    uint32_t fixed_i_qp;
    uint32_t fixed_p_qp;
};

int fh_h264_public_rc_parse(const char *name, uint32_t *public_mode);
int fh_h264_apollo_rcmode_parse(const char *name, uint32_t *app_mode);
int fh_h264_stock_resolve_app_mode(uint32_t public_mode, uint32_t global_app_mode, uint32_t *resolved_app_mode);

int fh_h264_app_mode_to_wire(uint32_t app_mode, uint32_t *wire_mode);
int fh_h264_wire_mode_to_app(uint32_t wire_mode, uint32_t *app_mode);
int fh_h264_app_rc_to_wire(uint32_t channel, const struct fh_h264_app_rc *app,
                           struct fh_pae_rc_wire *wire);
int fh_h264_wire_rc_to_app(const struct fh_pae_rc_wire *wire,
                           struct fh_h264_app_rc *app);

/* Stock IDR-with-QP helper semantics: mutate app-level init_qp only. */
int fh_h264_app_rc_set_idr_qp(struct fh_h264_app_rc *app, uint32_t requested_qp);

#endif
