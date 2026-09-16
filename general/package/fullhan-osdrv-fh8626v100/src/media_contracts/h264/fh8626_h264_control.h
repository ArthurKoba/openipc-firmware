#ifndef FH8626_H264_CONTROL_H
#define FH8626_H264_CONTROL_H

#include <stdint.h>
#include "fh8626_h264_rc.h"
#include "fh8626_h264_app_rc.h"

typedef int (*fh_h264_ioctl_fn)(void *opaque, unsigned long req, void *arg);

struct fh_h264_control {
    fh_h264_ioctl_fn ioctl;
    void *opaque;
    uint32_t channel;
};

/* RECONFIRMED hot operation. */
int fh_h264_start_recv(struct fh_h264_control *c);
int fh_h264_stop_recv(struct fh_h264_control *c);
int fh_h264_force_i(struct fh_h264_control *c);
int fh_h264_get_rc(struct fh_h264_control *c, struct fh_pae_rc_wire *out);

/* RECONFIRMED: full SET_RC rebuilds RC/SPS/PPS state; caller must use this on
 * a cold/quiesced/restart boundary rather than as a generic live mutation. */
int fh_h264_set_rc_cold(struct fh_h264_control *c, const struct fh_pae_rc_wire *next);

/* NEW: bounded live mutation path.  The exact 0x1c wire has no RC-mode field. */
int fh_h264_change_rc_realtime(struct fh_h264_control *c,
                               const struct fh_pae_rc_realtime_wire *next);

enum fh_h264_bitrate_apply_path {
    FH_H264_BITRATE_REALTIME = 0,
    FH_H264_BITRATE_RESTART_REQUIRED = 1
};
int fh_h264_bitrate_apply_path(uint32_t app_mode);
int fh_h264_build_realtime_bitrate(const struct fh_pae_rc_wire *current,
                                   uint32_t new_rate,
                                   struct fh_pae_rc_realtime_wire *out);

#endif
