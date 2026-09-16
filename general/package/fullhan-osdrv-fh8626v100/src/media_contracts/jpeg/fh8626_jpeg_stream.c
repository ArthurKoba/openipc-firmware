#include "fh8626_jpeg_stream.h"

#include <errno.h>
#include <string.h>

static int owner_valid(const struct fh_jpeg_stream_owner *o)
{
    return o && o->media_ioctl && o->jpeg_ioctl &&
           (o->mode == FH_JPEG_MODE_SNAPSHOT || o->mode == FH_JPEG_MODE_MJPEG);
}

int fh_jpeg_stream_release(struct fh_jpeg_stream_owner *owner)
{
    uint32_t mode;
    int rc;
    if (!owner_valid(owner) || !owner->held)
        return -EINVAL;
    mode = owner->mode;
    rc = owner->jpeg_ioctl(owner->opaque, FH_JPEG_RELEASE_STREAM, &mode);
    if (rc == 0)
        owner->held = 0;
    return rc;
}

static int cleanup(struct fh_jpeg_stream_owner *o, int err)
{
    int rc = fh_jpeg_stream_release(o);
    return rc ? rc : err;
}

int fh_jpeg_stream_acquire(struct fh_jpeg_stream_owner *owner,
                           const struct fh_jpeg_stream_map *map,
                           struct fh_jpeg_image_snapshot *out)
{
    struct fh_jpeg_query_wire q;
    uint64_t uoff, poff, end;
    const uint8_t *src;
    int rc;
    if (!owner_valid(owner) || !map || !map->bytes || !out || !out->bytes || owner->held)
        return -EINVAL;
    memset(&q, 0, sizeof(q));
    q.request_mask = owner->mode;
    rc = owner->media_ioctl(owner->opaque, FH_JPEG_MEDIA_QUERY_STREAM, &q);
    if (rc)
        return rc;
    owner->held = 1;
    if (q.returned_type != owner->mode || q.jpeg.mode != owner->mode ||
        q.jpeg.width == 0U || q.jpeg.height == 0U || q.jpeg.len < 4U ||
        q.jpeg.qp > FH_JPEG_MAX_QP_INDEX || q.jpeg.user < map->user_base ||
        q.jpeg.phys < map->phys_base)
        return cleanup(owner, -EPROTO);
    uoff = (uint64_t)q.jpeg.user - map->user_base;
    poff = (uint64_t)q.jpeg.phys - map->phys_base;
    end = uoff + q.jpeg.len;
    if (uoff != poff || end > map->size || end < uoff || q.jpeg.len > out->capacity)
        return cleanup(owner, -ERANGE);
    src = map->bytes + (size_t)uoff;
    if (src[0] != 0xffU || src[1] != 0xd8U ||
        src[q.jpeg.len - 2U] != 0xffU || src[q.jpeg.len - 1U] != 0xd9U)
        return cleanup(owner, -EPROTO);
    if (owner->generation == UINT64_MAX)
        return cleanup(owner, -EOVERFLOW);
    memcpy(out->bytes, src, q.jpeg.len);
    owner->generation++;
    out->size = q.jpeg.len;
    out->width = q.jpeg.width;
    out->height = q.jpeg.height;
    out->qp = q.jpeg.qp;
    out->mode = q.jpeg.mode;
    out->pts = ((uint64_t)q.jpeg.pts_hi << 32) | q.jpeg.pts_lo;
    out->generation = owner->generation;
    return 0;
}
