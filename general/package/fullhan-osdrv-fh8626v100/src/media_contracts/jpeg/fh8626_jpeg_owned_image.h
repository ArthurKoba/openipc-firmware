#ifndef FH8626_JPEG_OWNED_IMAGE_H
#define FH8626_JPEG_OWNED_IMAGE_H

#include <stddef.h>
#include <stdint.h>
#include "fh8626_jpeg_stream.h"

typedef void *(*fh_jpeg_realloc_fn)(void *opaque, void *ptr, size_t size);
struct fh_jpeg_owned_image {
    uint8_t *bytes;
    size_t capacity;
    size_t size;
    uint32_t width,height,qp,mode;
    uint64_t pts,generation;
};
int fh_jpeg_owned_image_assign(struct fh_jpeg_owned_image *dst,
                               const struct fh_jpeg_image_snapshot *src,
                               fh_jpeg_realloc_fn realloc_fn, void *opaque);
int fh_jpeg_owned_image_acquire_release_commit(struct fh_jpeg_owned_image *dst,
                               struct fh_jpeg_stream_owner *owner,
                               const struct fh_jpeg_stream_map *map,
                               struct fh_jpeg_image_snapshot *scratch,
                               fh_jpeg_realloc_fn realloc_fn, void *opaque);
#endif
