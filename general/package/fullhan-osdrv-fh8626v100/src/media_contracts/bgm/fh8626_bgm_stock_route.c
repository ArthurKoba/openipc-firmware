#define _GNU_SOURCE
#include "fh8626_bgm_stock_route.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int write_capability(struct fh_bgm_stock_route *r, const char *name, int enabled)
{
    char cmd[40];
    int n;
    n = snprintf(cmd, sizeof(cmd), "%s_%u_%u\n", name, r->channel, enabled ? 1u : 0u);
    if (n <= 0 || (size_t)n >= sizeof(cmd)) return -EOVERFLOW;
    return r->ops.write_text(r->ops.opaque, FH_ENC_PROC_PATH, cmd, (size_t)n);
}

int fh_bgm_stock_coarse_geometry(uint32_t w, uint32_t h,
                                 uint32_t *cw, uint32_t *ch)
{
    uint64_t aw, ah;
    if (!w || !h || !cw || !ch) return -EINVAL;
    aw = (uint64_t)w + 15u;
    ah = (uint64_t)h + 15u;
    if (aw > UINT32_MAX || ah > UINT32_MAX) return -EOVERFLOW;
    *cw = (uint32_t)((aw >> 4) << 1);
    *ch = (uint32_t)((ah >> 4) << 1);
    if (!*cw || !*ch) return -ERANGE;
    return 0;
}

int fh_bgm_stock_route_init(struct fh_bgm_stock_route *r,
                            const struct fh_bgm_route_ops *ops,
                            unsigned channel)
{
    if (!r || !ops || !ops->write_text || !ops->read_text ||
        !ops->media_ioctl || channel > 7u)
        return -EINVAL;
    memset(r, 0, sizeof(*r));
    r->ops = *ops;
    r->channel = channel;
    r->state = FH_BGM_ROUTE_FRESH;
    return 0;
}

int fh_bgm_stock_prepare_encoder_capability(struct fh_bgm_stock_route *r,
                                            int texture_enable,
                                            int bgm_enable)
{
    int rc;
    if (!r) return -EINVAL;
    texture_enable = !!texture_enable;
    bgm_enable = !!bgm_enable;
    if (bgm_enable && !texture_enable) return -EINVAL;
    if (r->state == FH_BGM_ROUTE_BOUND || r->state == FH_BGM_ROUTE_RECOVERY_REQUIRED)
        return -EBUSY;

    /* NEW: stock parser is underscore-delimited. Explicitly write both values so
     * a new cold configuration cannot inherit process-global capability state. */
    rc = write_capability(r, "texture", texture_enable);
    if (rc) {
        r->state = FH_BGM_ROUTE_RECOVERY_REQUIRED;
        return rc;
    }
    rc = write_capability(r, "bgm", bgm_enable);
    if (rc) {
        /* The texture write may already have changed global driver state. */
        r->state = FH_BGM_ROUTE_RECOVERY_REQUIRED;
        return rc;
    }

    r->requested_texture = texture_enable;
    r->requested_bgm = bgm_enable;
    r->effective_texture = 0;
    r->effective_bgm = 0;
    r->state = FH_BGM_ROUTE_PREPARED;
    return 0;
}

static const char *find_channel_section(const char *text, unsigned channel)
{
    char marker[48];
    int n;
    const char *p;
    n = snprintf(marker, sizeof(marker), "****<Channel-%u ", channel);
    if (n <= 0 || (size_t)n >= sizeof(marker)) return NULL;
    p = strstr(text, marker);
    if (p) return p;

    /* Some proc revisions omit the status word immediately after the number. */
    n = snprintf(marker, sizeof(marker), "****<Channel-%u>", channel);
    if (n <= 0 || (size_t)n >= sizeof(marker)) return NULL;
    return strstr(text, marker);
}

int fh_bgm_parse_enc_effective(const char *text, unsigned channel,
                               int *texture_enabled, int *bgm_enabled)
{
    const char *start, *end, *func;
    size_t span;
    int tex, bgm;
    if (!text || !texture_enabled || !bgm_enabled || channel > 7u) return -EINVAL;
    start = find_channel_section(text, channel);
    if (!start) return -ENOENT;
    end = strstr(start + 4, "****<Channel-");
    if (!end) end = start + strlen(start);
    func = strstr(start, "## Function:");
    if (!func || func >= end) return -EPROTO;
    span = (size_t)(end - func);
    tex = memmem(func, span, "[TEXTURE]", 9) != NULL;
    bgm = memmem(func, span, "[BGM]", 5) != NULL;
    *texture_enabled = tex;
    *bgm_enabled = bgm;
    return 0;
}

int fh_bgm_stock_verify_encoder_capability(struct fh_bgm_stock_route *r)
{
    char buf[32768];
    size_t used = 0;
    int tex = 0, bgm = 0, rc;
    if (!r) return -EINVAL;
    if (r->state != FH_BGM_ROUTE_PREPARED) return -EPERM;
    rc = r->ops.read_text(r->ops.opaque, FH_ENC_PROC_PATH, buf, sizeof(buf), &used);
    if (rc) return rc;
    if (!used || used >= sizeof(buf)) return -EOVERFLOW;
    buf[used] = '\0';
    rc = fh_bgm_parse_enc_effective(buf, r->channel, &tex, &bgm);
    if (rc) return rc;
    r->effective_texture = tex;
    r->effective_bgm = bgm;
    if (tex != r->requested_texture || bgm != r->requested_bgm) {
        /* The cold encoder snapshot does not match the requested global
         * capability state. Do not reuse this epoch or permit a bind. */
        r->state = FH_BGM_ROUTE_RECOVERY_REQUIRED;
        return -EIO;
    }
    r->state = FH_BGM_ROUTE_VERIFIED;
    return 0;
}

int fh_bgm_stock_bind(struct fh_bgm_stock_route *r)
{
    struct fh_media_bind_wire pair = { FH_MEDIA_OBJ_VPU_BGM_SRC, FH_MEDIA_OBJ_BGM };
    int rc;
    if (!r) return -EINVAL;
    if (r->state == FH_BGM_ROUTE_BOUND) return 0;
    if (r->state == FH_BGM_ROUTE_RECOVERY_REQUIRED) return -EUCLEAN;
    if (r->state != FH_BGM_ROUTE_VERIFIED || !r->effective_texture || !r->effective_bgm)
        return -EPERM;
    rc = r->ops.media_ioctl(r->ops.opaque, FH_MEDIA_BIND, &pair, sizeof(pair));
    if (rc) {
        /* NEW: ioctl failure does not prove absence of a side effect. */
        r->state = FH_BGM_ROUTE_RECOVERY_REQUIRED;
        return rc;
    }
    r->state = FH_BGM_ROUTE_BOUND;
    return 0;
}

int fh_bgm_stock_unbind(struct fh_bgm_stock_route *r)
{
    struct fh_media_unbind_src_wire source = { FH_MEDIA_OBJ_VPU_BGM_SRC };
    int rc;
    if (!r) return -EINVAL;
    if (r->state == FH_BGM_ROUTE_RECOVERY_REQUIRED) return -EUCLEAN;
    if (r->state != FH_BGM_ROUTE_BOUND) return 0;
    rc = r->ops.media_ioctl(r->ops.opaque, FH_MEDIA_UNBIND_SRC,
                            &source, sizeof(source));
    if (rc) {
        r->state = FH_BGM_ROUTE_RECOVERY_REQUIRED;
        return rc;
    }
    /* Capabilities remain the verified snapshot for this cold encoder epoch. */
    r->state = FH_BGM_ROUTE_VERIFIED;
    return 0;
}

int fh_bgm_stock_route_needs_recovery(const struct fh_bgm_stock_route *r)
{
    return r && r->state == FH_BGM_ROUTE_RECOVERY_REQUIRED;
}

int fh_bgm_stock_route_begin_after_external_recovery(struct fh_bgm_stock_route *r)
{
    if (!r || r->state != FH_BGM_ROUTE_RECOVERY_REQUIRED)
        return -EINVAL;
    r->requested_texture = 0;
    r->requested_bgm = 0;
    r->effective_texture = 0;
    r->effective_bgm = 0;
    r->state = FH_BGM_ROUTE_FRESH;
    return 0;
}

int fh_bgm_linux_write_text(void *opaque, const char *path,
                            const char *text, size_t len)
{
    int fd;
    size_t off = 0;
    (void)opaque;
    if (!path || !text || !len) return -EINVAL;
    fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0) return -errno;
    while (off < len) {
        ssize_t n = write(fd, text + off, len - off);
        if (n < 0) {
            int e = errno;
            close(fd);
            return -e;
        }
        if (!n) {
            close(fd);
            return -EIO;
        }
        off += (size_t)n;
    }
    if (close(fd) < 0) return -errno;
    return 0;
}

int fh_bgm_linux_read_text(void *opaque, const char *path,
                           char *buf, size_t cap, size_t *used)
{
    int fd;
    size_t off = 0;
    (void)opaque;
    if (!path || !buf || cap < 2 || !used) return -EINVAL;
    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -errno;
    while (off + 1 < cap) {
        ssize_t n = read(fd, buf + off, cap - 1 - off);
        if (n < 0) {
            int e = errno;
            close(fd);
            return -e;
        }
        if (!n) break;
        off += (size_t)n;
    }
    if (close(fd) < 0) return -errno;
    if (off + 1 == cap) return -EOVERFLOW;
    buf[off] = '\0';
    *used = off;
    return 0;
}

int fh_bgm_linux_media_ioctl(void *opaque, unsigned long req,
                             void *arg, size_t arg_size)
{
    const int *fdp = opaque;
    int rc;
    if (!fdp || *fdp < 0 || !arg) return -EINVAL;
    if ((req == FH_MEDIA_BIND && arg_size != sizeof(struct fh_media_bind_wire)) ||
        (req == FH_MEDIA_UNBIND_SRC && arg_size != sizeof(struct fh_media_unbind_src_wire)))
        return -EINVAL;
    rc = ioctl(*fdp, req, arg);
    if (rc < 0) return -errno;
    if (rc > 0) return -EIO;
    return 0;
}
