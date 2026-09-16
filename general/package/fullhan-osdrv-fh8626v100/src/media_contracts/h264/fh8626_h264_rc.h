#ifndef FH8626_H264_RC_H
#define FH8626_H264_RC_H

#include <stddef.h>
#include <stdint.h>

#define FH_PAE_SET_RC_CONFIG        0xC054502FUL
#define FH_PAE_GET_RC_CONFIG        0xC0545030UL
#define FH_PAE_START_RECV           0xC0045008UL
#define FH_PAE_STOP_RECV            0xC0045009UL
#define FH_PAE_FORCE_I              0xC0045014UL
#define FH_PAE_CHANGE_RC_REALTIME   0xC01C5055UL
#define FH_PAE_MAX_CHANNELS         8U
#define FH_PAE_MAX_QP               51U

/* WIRE_CONFIRMED / SEMANTICS_CONFIRMED where named below.
 * Numeric mode 3 is accepted by the driver, but its public/vendor semantic
 * label is not proved and must not be guessed. */
enum fh_pae_rc_mode {
    FH_PAE_RC_VBR = 0,
    FH_PAE_RC_CBR = 1,
    FH_PAE_RC_FIXED_QP = 2,
    FH_PAE_RC_MODE3_UNRESOLVED = 3,
    FH_PAE_RC_AVBR = 4,
    FH_PAE_RC_CVBR = 5
};

/* WIRE_CONFIRMED: exact 0x54-byte full RC ioctl including channel.
 * Operational names for 0x3c..0x50 follow the recovered consumer semantics;
 * vendor/public SDK names for some fields remain unknown. */
struct fh_pae_rc_wire {
    uint32_t chn;                    /* 0x00 */
    uint32_t rc_mode;                /* 0x04, wire 0..5 */
    uint32_t frame_rate_packed;      /* 0x08 */
    uint32_t mode2_qp_a;             /* 0x0c */
    uint32_t mode2_qp_b;             /* 0x10 */
    uint32_t init_qp;                /* 0x14 */
    uint32_t bitrate_or_rate;        /* 0x18 */
    uint32_t i_min_qp;               /* 0x1c */
    uint32_t i_max_qp;               /* 0x20 */
    uint32_t p_min_qp;               /* 0x24 */
    uint32_t p_max_qp;               /* 0x28 */
    uint32_t i_proportion;           /* 0x2c */
    uint32_t p_proportion;           /* 0x30 */
    uint32_t fluctuate_level;        /* 0x34 */
    int32_t  ip_qp_delta;            /* 0x38 */
    uint32_t i_target_limit_bits;    /* 0x3c; consumed as >>3 */
    uint32_t max_rate_percent;       /* 0x40 */
    uint32_t still_rate_percent;     /* 0x44; mode4 outer validation 25..100 */
    uint32_t max_still_qp;           /* 0x48; mode4 outer validation 0..51 */
    uint32_t additional_rate_bits;   /* 0x4c; mode5 secondary rate, >>3 */
    uint32_t extra_qp_parameter;     /* 0x50; consumer semantic proved, public name unresolved */
};
_Static_assert(sizeof(struct fh_pae_rc_wire) == 0x54, "PAE full RC wire ABI");
_Static_assert(offsetof(struct fh_pae_rc_wire, chn) == 0x00, "PAE full RC channel offset");
_Static_assert(offsetof(struct fh_pae_rc_wire, rc_mode) == 0x04, "PAE full RC mode offset");
_Static_assert(offsetof(struct fh_pae_rc_wire, ip_qp_delta) == 0x38, "PAE full RC IP delta offset");
_Static_assert(offsetof(struct fh_pae_rc_wire, i_target_limit_bits) == 0x3c, "PAE full RC I-target limit offset");
_Static_assert(offsetof(struct fh_pae_rc_wire, max_rate_percent) == 0x40, "PAE full RC max-rate offset");
_Static_assert(offsetof(struct fh_pae_rc_wire, still_rate_percent) == 0x44, "PAE full RC still-rate offset");
_Static_assert(offsetof(struct fh_pae_rc_wire, max_still_qp) == 0x48, "PAE full RC max-still-QP offset");
_Static_assert(offsetof(struct fh_pae_rc_wire, additional_rate_bits) == 0x4c, "PAE full RC additional-rate offset");
_Static_assert(offsetof(struct fh_pae_rc_wire, extra_qp_parameter) == 0x50, "PAE full RC extra-QP offset");

/* WIRE_CONFIRMED: exact 0x1c realtime RC ioctl payload recovered from
 * pae_change_rc_realtime(). There is deliberately no rc_mode member. */
struct fh_pae_rc_realtime_wire {
    uint32_t chn;                 /* 0x00 */
    uint32_t rate;                /* 0x04; target consumes rate >> 3 */
    uint32_t frame_rate_packed;   /* 0x08; two non-zero 16-bit values */
    uint32_t i_min_qp;            /* 0x0c */
    uint32_t i_max_qp;            /* 0x10 */
    uint32_t p_min_qp;            /* 0x14 */
    uint32_t p_max_qp;            /* 0x18 */
};
_Static_assert(sizeof(struct fh_pae_rc_realtime_wire) == 0x1c, "PAE realtime RC wire ABI");
_Static_assert(offsetof(struct fh_pae_rc_realtime_wire, rate) == 0x04, "PAE realtime rate offset");
_Static_assert(offsetof(struct fh_pae_rc_realtime_wire, frame_rate_packed) == 0x08, "PAE realtime FPS offset");
_Static_assert(offsetof(struct fh_pae_rc_realtime_wire, p_max_qp) == 0x18, "PAE realtime P max QP offset");

static inline int fh_pae_channel_valid(uint32_t chn)
{
    return chn < FH_PAE_MAX_CHANNELS;
}

static inline int fh_pae_fps_packed_valid(uint32_t packed)
{
    uint32_t low = packed & 0xffffU;
    uint32_t high = packed >> 16;
    return low != 0U && high != 0U;
}

/* Exact driver-side bound: rate_control_init validates each QP independently.
 * It does not impose min<=max at the outer validation layer. */
static inline int fh_pae_qp_bounds_driver_valid(uint32_t min_qp, uint32_t max_qp)
{
    return min_qp <= FH_PAE_MAX_QP && max_qp <= FH_PAE_MAX_QP;
}

/* Optional SDK policy helper. This is intentionally separate from the exact
 * driver validator so callers may choose stricter policy without rewriting
 * the recovered ABI contract. */
static inline int fh_pae_qp_pair_policy_valid(uint32_t min_qp, uint32_t max_qp)
{
    return fh_pae_qp_bounds_driver_valid(min_qp, max_qp) && min_qp <= max_qp;
}

/* WIRE/DRIVER_CONFIRMED validation reconstructed from rate_control_init().
 * Fixed-QP wire mode 2 takes its own early branch after FPS validation and
 * therefore does not inherit the common native-RC field checks below. */
static inline int fh_pae_rc_validate_driver(const struct fh_pae_rc_wire *next)
{
    if (!next || !fh_pae_channel_valid(next->chn) || !fh_pae_fps_packed_valid(next->frame_rate_packed))
        return -1;
    if (next->rc_mode > FH_PAE_RC_CVBR)
        return -1;
    if (next->rc_mode == FH_PAE_RC_FIXED_QP)
        return (next->mode2_qp_a <= FH_PAE_MAX_QP && next->mode2_qp_b <= FH_PAE_MAX_QP) ? 0 : -1;
    if (next->init_qp > FH_PAE_MAX_QP)
        return -1;
    if (!fh_pae_qp_bounds_driver_valid(next->i_min_qp, next->i_max_qp) ||
        !fh_pae_qp_bounds_driver_valid(next->p_min_qp, next->p_max_qp))
        return -1;
    if (!next->i_proportion || !next->p_proportion || next->fluctuate_level > 6U ||
        next->ip_qp_delta < -51 || next->ip_qp_delta > 51)
        return -1;
    if (next->max_rate_percent < 110U || next->max_rate_percent > 800U)
        return -1;
    if (next->rc_mode == FH_PAE_RC_AVBR &&
        (next->still_rate_percent < 25U || next->still_rate_percent > 100U ||
         next->max_still_qp > FH_PAE_MAX_QP))
        return -1;
    return 0;
}

/* Optional stricter SDK policy, deliberately not used by the exact ioctl
 * wrapper. It currently adds only QP ordering on top of the driver contract. */
static inline int fh_pae_rc_validate_sdk_policy(const struct fh_pae_rc_wire *next)
{
    if (fh_pae_rc_validate_driver(next))
        return -1;
    if (next->rc_mode == FH_PAE_RC_FIXED_QP)
        return 0;
    if (!fh_pae_qp_pair_policy_valid(next->i_min_qp, next->i_max_qp) ||
        !fh_pae_qp_pair_policy_valid(next->p_min_qp, next->p_max_qp))
        return -1;
    return 0;
}

/* Exact realtime outer-path validation: channel/FPS and individual QP bounds.
 * No rate bound or min<=max relation is invented here. */
static inline int fh_pae_rc_realtime_validate_driver(const struct fh_pae_rc_realtime_wire *next)
{
    if (!next || !fh_pae_channel_valid(next->chn))
        return -1;
    if (!fh_pae_fps_packed_valid(next->frame_rate_packed))
        return -1;
    if (!fh_pae_qp_bounds_driver_valid(next->i_min_qp, next->i_max_qp) ||
        !fh_pae_qp_bounds_driver_valid(next->p_min_qp, next->p_max_qp))
        return -1;
    return 0;
}

#endif
