#include "fh8626_h264_stream.h"

#include <errno.h>
#include <string.h>

static int owner_valid(const struct fh_h264_stream_owner *o)
{
    return o && o->media_ioctl && o->pae_ioctl && o->channel < 8U;
}

static int entry_offset(const struct fh_h264_stream_map *m,
                        const struct fh_h264_stream_entry_wire *e,
                        size_t *off)
{
    uint64_t uoff, poff, end;
    if (!m || !m->bytes || !e || !off || e->len == 0U)
        return -EINVAL;
    if (e->user < m->user_base || e->phys < m->phys_base)
        return -ERANGE;
    uoff = (uint64_t)e->user - m->user_base;
    poff = (uint64_t)e->phys - m->phys_base;
    if (uoff != poff)
        return -ERANGE;
    end = uoff + e->len;
    if (end > m->size || end < uoff)
        return -ERANGE;
    *off = (size_t)uoff;
    return 0;
}

int fh_h264_stream_release(struct fh_h264_stream_owner *owner)
{
    uint32_t ch;
    int rc;
    if (!owner_valid(owner) || !owner->held)
        return -EINVAL;
    ch = owner->channel;
    rc = owner->pae_ioctl(owner->opaque, FH_PAE_RELEASE_STREAM, &ch);
    if (rc == 0)
        owner->held = 0;
    return rc;
}

static int cleanup_failed_acquire(struct fh_h264_stream_owner *o, int error)
{
    int rr = fh_h264_stream_release(o);
    return rr ? rr : error;
}

int fh_h264_stream_acquire(struct fh_h264_stream_owner *owner,
                           const struct fh_h264_stream_map *map,
                           struct fh_h264_au_snapshot *out)
{
    struct fh_media_stream_query_wire q;
    size_t i, total = 0;
    int rc;
    if (!owner_valid(owner) || !map || !out || !out->bytes || owner->held)
        return -EINVAL;
    memset(&q, 0, sizeof(q));
    q.request_mask = FH_H264_MEDIA_TYPE;
    rc = owner->media_ioctl(owner->opaque, FH_MEDIA_QUERY_STREAM, &q);
    if (rc)
        return rc;
    owner->held = 1;
    if (q.returned_type != FH_H264_MEDIA_TYPE || q.h264.type != FH_H264_MEDIA_TYPE ||
        q.h264.channel != owner->channel || q.h264.entry_count == 0U ||
        q.h264.entry_count > FH_H264_MAX_ENTRIES)
        return cleanup_failed_acquire(owner, -EPROTO);

    for (i = 0; i < q.h264.entry_count; ++i) {
        size_t off, j;
        uint64_t next;
        rc = entry_offset(map, &q.h264.entry[i], &off);
        if (rc)
            return cleanup_failed_acquire(owner, rc);
        for (j = 0; j < i; ++j) {
            size_t prev;
            uint64_t a0, a1, b0, b1;
            rc = entry_offset(map, &q.h264.entry[j], &prev);
            if (rc)
                return cleanup_failed_acquire(owner, rc);
            a0 = off; a1 = a0 + q.h264.entry[i].len;
            b0 = prev; b1 = b0 + q.h264.entry[j].len;
            if (a0 < b1 && b0 < a1)
                return cleanup_failed_acquire(owner, -EPROTO);
        }
        next = (uint64_t)total + q.h264.entry[i].len;
        if (next > out->capacity || next < total)
            return cleanup_failed_acquire(owner, -ENOSPC);
        out->span[i].offset = total;
        out->span[i].len = q.h264.entry[i].len;
        memcpy(out->bytes + total, map->bytes + off, q.h264.entry[i].len);
        total = (size_t)next;
    }
    if (owner->generation == UINT64_MAX)
        return cleanup_failed_acquire(owner, -EOVERFLOW);
    owner->generation++;
    out->size = total;
    out->pts = ((uint64_t)q.h264.pts_hi << 32) | q.h264.pts_lo;
    out->generation = owner->generation;
    out->channel = q.h264.channel;
    out->frame_class = q.h264.frame_class;
    out->entry_count = q.h264.entry_count;
    return 0;
}
