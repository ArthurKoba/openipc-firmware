#ifndef FH8626_JPEG_LIFECYCLE_H
#define FH8626_JPEG_LIFECYCLE_H

#include <stdint.h>

#define FH_JPEG_YUV_SUBMIT 0xC0304A0FUL

struct fh_jpeg_yuv_submit_wire {
    uint32_t mode;          /* +0x00: 1 snapshot, 2 MJPEG */
    uint32_t width;         /* +0x04 */
    uint32_t height;        /* +0x08 */
    uint32_t y_phys;        /* +0x0c */
    uint32_t c_phys;        /* +0x10 */
    uint32_t unresolved_14; /* +0x14 */
    uint32_t pts_lo;        /* +0x18 */
    uint32_t pts_hi;        /* +0x1c */
    uint32_t input_selector;/* +0x20: stock accepts 1 */
    uint32_t effective_w;   /* +0x24 */
    uint32_t effective_h;   /* +0x28 */
    uint32_t unresolved_2c; /* +0x2c */
};
_Static_assert(sizeof(struct fh_jpeg_yuv_submit_wire) == 0x30, "JPEG YUV submit ABI");

enum fh_jpeg_lifecycle_state {
    FH_JPEG_LC_EMPTY = 0,
    FH_JPEG_LC_MEMORY_READY,
    FH_JPEG_LC_CONFIGURED,
    FH_JPEG_LC_RUNNING,
    FH_JPEG_LC_STOPPED,
    FH_JPEG_LC_QUARANTINED
};

struct fh_jpeg_lifecycle_ops {
    int (*mem_init)(void *opaque);
    int (*mem_uninit)(void *opaque);
    int (*set_config)(void *opaque, uint32_t mode);
    int (*set_drop)(void *opaque);
    int (*start)(void *opaque);
    int (*stop)(void *opaque);
    int (*submit)(void *opaque, const struct fh_jpeg_yuv_submit_wire *wire);
};

struct fh_jpeg_lifecycle {
    const struct fh_jpeg_lifecycle_ops *ops;
    void *opaque;
    enum fh_jpeg_lifecycle_state state;
    uint32_t mode;
    uint64_t generation;
    uint64_t input_generation;
    uint64_t input_pts;
    int input_held;
    int output_held;
    int snapshot_auto_started;
    int vmm_release_ready;
};

void fh_jpeg_lifecycle_init(struct fh_jpeg_lifecycle *lc,
                            const struct fh_jpeg_lifecycle_ops *ops, void *opaque);
int fh_jpeg_lifecycle_mem_init(struct fh_jpeg_lifecycle *lc);
int fh_jpeg_lifecycle_configure(struct fh_jpeg_lifecycle *lc, uint32_t mode, int snapshot_auto_started);
int fh_jpeg_lifecycle_apply_drop(struct fh_jpeg_lifecycle *lc);
int fh_jpeg_lifecycle_start(struct fh_jpeg_lifecycle *lc);
int fh_jpeg_lifecycle_submit(struct fh_jpeg_lifecycle *lc, const struct fh_jpeg_yuv_submit_wire *wire,
                             uint64_t *input_generation);
int fh_jpeg_lifecycle_observe_output_pts(struct fh_jpeg_lifecycle *lc, uint64_t pts);
int fh_jpeg_lifecycle_stop(struct fh_jpeg_lifecycle *lc);
int fh_jpeg_lifecycle_mem_uninit(struct fh_jpeg_lifecycle *lc);
void fh_jpeg_lifecycle_set_output_held(struct fh_jpeg_lifecycle *lc, int held);

#endif
