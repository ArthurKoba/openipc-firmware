#ifndef FH8626_JPEG_STREAM_H
#define FH8626_JPEG_STREAM_H

#include <stddef.h>
#include <stdint.h>

#define FH_JPEG_MEDIA_QUERY_STREAM 0xC1704D06UL
#define FH_JPEG_RELEASE_STREAM 0xC0044A10UL
#define FH_JPEG_MODE_SNAPSHOT 1U
#define FH_JPEG_MODE_MJPEG 2U
#define FH_JPEG_MAX_QP_INDEX 98U

struct fh_jpeg_stream_desc_wire {
    uint32_t mode;
    uint32_t width;
    uint32_t height;
    uint32_t phys;
    uint32_t user;
    uint32_t len;
    uint32_t pts_lo;
    uint32_t pts_hi;
    uint32_t qp;
    uint32_t unresolved_24;
};
_Static_assert(sizeof(struct fh_jpeg_stream_desc_wire) == 0x28, "JPEG stream descriptor ABI");

struct fh_jpeg_query_wire {
    uint32_t request_mask;
    uint32_t returned_type;
    struct fh_jpeg_stream_desc_wire jpeg;
    uint8_t padding[0x170 - 8 - 0x28];
};
_Static_assert(sizeof(struct fh_jpeg_query_wire) == 0x170, "JPEG media query ABI");

typedef int (*fh_jpeg_stream_ioctl_fn)(void *opaque, unsigned long req, void *arg);

struct fh_jpeg_stream_map {
    uint32_t phys_base;
    uint32_t user_base;
    size_t size;
    const uint8_t *bytes;
};
struct fh_jpeg_image_snapshot {
    uint8_t *bytes;
    size_t capacity;
    size_t size;
    uint32_t width, height, qp, mode;
    uint64_t pts, generation;
};
struct fh_jpeg_stream_owner {
    fh_jpeg_stream_ioctl_fn media_ioctl;
    fh_jpeg_stream_ioctl_fn jpeg_ioctl;
    void *opaque;
    uint32_t mode;
    uint64_t generation;
    int held;
};

int fh_jpeg_stream_acquire(struct fh_jpeg_stream_owner *owner,
                           const struct fh_jpeg_stream_map *map,
                           struct fh_jpeg_image_snapshot *out);
int fh_jpeg_stream_release(struct fh_jpeg_stream_owner *owner);

#endif
