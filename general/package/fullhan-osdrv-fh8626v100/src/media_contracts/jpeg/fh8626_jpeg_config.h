#ifndef FH8626_JPEG_CONFIG_H
#define FH8626_JPEG_CONFIG_H

#include <stdint.h>

#define FH_JPEG_SET_CFG       0xC0344A05UL
#define FH_JPEG_GET_CFG       0xC0344A06UL
#define FH_JPEG_SET_RC_UNWIRED 0xC01C4A07UL
#define FH_JPEG_GET_RC_UNWIRED 0xC01C4A08UL
#define FH_JPEG_SET_DROP_CFG  0xC0244A0BUL
#define FH_JPEG_GET_DROP_CFG  0xC0244A0CUL
#define FH_JPEG_QP_MAX 98U

struct fh_jpeg_cfg_wire {
    uint32_t mode;
    uint32_t width;
    uint32_t height;
    uint32_t src_fps_packed;
    uint32_t dst_fps_packed;
    uint32_t rc_selector;
    uint32_t qp;
    uint32_t target_rate;
    uint32_t min_qp;
    uint32_t max_qp;
    uint32_t rate_selector;
    uint32_t secondary_rate;
    uint32_t rotation;
};
_Static_assert(sizeof(struct fh_jpeg_cfg_wire) == 0x34, "JPEG full config ABI");

struct fh_jpeg_drop_wire {
    uint32_t src_fps_packed;          /* +0x00 */
    uint32_t fixed_dst_fps_packed;    /* +0x04 */
    uint32_t qp_threshold;            /* +0x08 */
    uint32_t qp_fps_packed;           /* +0x0c */
    uint32_t instant_rate_enable;     /* +0x10 */
    uint32_t validator_fps_packed;    /* +0x14: stock validator reads here */
    uint32_t instant_rate_threshold;  /* +0x18 */
    uint32_t reserved_1c;             /* +0x1c */
    uint32_t instant_rate_fps_packed; /* +0x20: runtime consumes here */
};
_Static_assert(sizeof(struct fh_jpeg_drop_wire) == 0x24, "JPEG drop config ABI");

extern const uint32_t fh_jpeg_rate_selector_words[10];
int fh_jpeg_cfg_validate_driver(const struct fh_jpeg_cfg_wire *cfg);
int fh_jpeg_cfg_validate_sdk(const struct fh_jpeg_cfg_wire *cfg);
int fh_jpeg_drop_validate_driver(const struct fh_jpeg_drop_wire *drop);
int fh_jpeg_drop_validate_sdk(const struct fh_jpeg_drop_wire *drop);
int fh_jpeg_drop_build_safe(struct fh_jpeg_drop_wire *out,
                            uint32_t src_fps_packed,
                            uint32_t fixed_dst_fps_packed,
                            uint32_t qp_threshold,
                            uint32_t qp_fps_packed,
                            uint32_t instant_rate_threshold,
                            uint32_t instant_rate_fps_packed);

#endif
