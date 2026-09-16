#include "fh8626_jpeg_owned_image.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

int fh_jpeg_owned_image_assign(struct fh_jpeg_owned_image *dst,
                               const struct fh_jpeg_image_snapshot *src,
                               fh_jpeg_realloc_fn realloc_fn, void *opaque)
{
    uint8_t *next;
    if(!dst||!src||!src->bytes||!realloc_fn||src->size==0) return -EINVAL;
    if(src->generation==0) return -EINVAL;
    if(src->size>dst->capacity){
        next=realloc_fn(opaque,dst->bytes,src->size);
        if(!next) return -ENOMEM;
    } else next=dst->bytes;
    memcpy(next,src->bytes,src->size);
    dst->bytes=next; if(src->size>dst->capacity) dst->capacity=src->size;
    dst->size=src->size; dst->width=src->width; dst->height=src->height; dst->qp=src->qp; dst->mode=src->mode;
    dst->pts=src->pts; dst->generation=src->generation; return 0;
}
int fh_jpeg_owned_image_acquire_release_commit(struct fh_jpeg_owned_image *dst,
                               struct fh_jpeg_stream_owner *owner,
                               const struct fh_jpeg_stream_map *map,
                               struct fh_jpeg_image_snapshot *scratch,
                               fh_jpeg_realloc_fn realloc_fn, void *opaque)
{
    int rc;
    if(!dst||!owner||!map||!scratch) return -EINVAL;
    rc=fh_jpeg_stream_acquire(owner,map,scratch); if(rc) return rc;
    rc=fh_jpeg_stream_release(owner); if(rc) return rc;
    return fh_jpeg_owned_image_assign(dst,scratch,realloc_fn,opaque);
}
