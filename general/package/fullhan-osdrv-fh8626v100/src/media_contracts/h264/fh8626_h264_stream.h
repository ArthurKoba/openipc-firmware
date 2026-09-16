#ifndef FH8626_H264_STREAM_H
#define FH8626_H264_STREAM_H

#include <stddef.h>
#include <stdint.h>

#define FH_MEDIA_QUERY_STREAM 0xC1704D06UL
#define FH_PAE_RELEASE_STREAM 0xC0045011UL
#define FH_H264_MEDIA_TYPE 4U
#define FH_H264_MAX_ENTRIES 20U

struct fh_h264_stream_entry_wire {
    uint32_t metadata;
    uint32_t phys;
    uint32_t user;
    uint32_t len;
};

struct fh_h264_stream_desc_wire {
    uint32_t type;
    uint32_t channel;
    uint32_t entry_count;
    uint32_t frame_class;
    uint32_t phys_start;
    uint32_t user_start;
    uint32_t occupied_extent;
    uint32_t unresolved_1c;
    uint32_t pts_lo;
    uint32_t pts_hi;
    struct fh_h264_stream_entry_wire entry[FH_H264_MAX_ENTRIES];
};

struct fh_media_stream_query_wire {
    uint32_t request_mask;
    uint32_t returned_type;
    struct fh_h264_stream_desc_wire h264;
};

_Static_assert(sizeof(struct fh_h264_stream_entry_wire) == 0x10, "H264 stream entry ABI");
_Static_assert(sizeof(struct fh_h264_stream_desc_wire) == 0x168, "H264 stream descriptor ABI");
_Static_assert(sizeof(struct fh_media_stream_query_wire) == 0x170, "media stream query ABI");

typedef int (*fh_h264_stream_ioctl_fn)(void *opaque, unsigned long req, void *arg);

struct fh_h264_stream_map {
    uint32_t phys_base;
    uint32_t user_base;
    size_t size;
    const uint8_t *bytes;
};

struct fh_h264_au_span {
    size_t offset;
    size_t len;
};

struct fh_h264_au_snapshot {
    uint8_t *bytes;
    size_t capacity;
    size_t size;
    uint64_t pts;
    uint64_t generation;
    uint32_t channel;
    uint32_t frame_class;
    uint32_t entry_count;
    struct fh_h264_au_span span[FH_H264_MAX_ENTRIES];
};

struct fh_h264_stream_owner {
    fh_h264_stream_ioctl_fn media_ioctl;
    fh_h264_stream_ioctl_fn pae_ioctl;
    void *opaque;
    uint32_t channel;
    uint64_t generation;
    int held;
};

int fh_h264_stream_acquire(struct fh_h264_stream_owner *owner,
                           const struct fh_h264_stream_map *map,
                           struct fh_h264_au_snapshot *out);
int fh_h264_stream_release(struct fh_h264_stream_owner *owner);

#endif
