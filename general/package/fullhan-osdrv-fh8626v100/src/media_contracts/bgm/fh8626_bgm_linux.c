#define _GNU_SOURCE
#include "fh8626_bgm_linux.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int real_open(void *opaque, const char *path, int flags)
{
    (void)opaque;
    return open(path, flags);
}

static int real_close(void *opaque, int fd)
{
    (void)opaque;
    return close(fd);
}

static int real_ioctl(void *opaque, int fd, unsigned long req, void *arg)
{
    (void)opaque;
    return ioctl(fd, req, arg);
}

static const struct fh_bgm_linux_sys_ops default_sys = {
    real_open, real_close, real_ioctl, NULL
};

static int sys_ioctl_zero(struct fh_bgm_linux *l, int fd, unsigned long req, void *arg)
{
    int rc;
    errno = 0;
    rc = l->sys.ioctl_fn(l->sys.opaque, fd, req, arg);
    if (rc < 0)
        return errno ? -errno : -EIO;
    if (rc > 0)
        return -EIO;
    return 0;
}

static int sys_close(struct fh_bgm_linux *l, int fd)
{
    int rc;
    errno = 0;
    rc = l->sys.close_fn(l->sys.opaque, fd);
    if (rc < 0)
        return errno ? -errno : -EIO;
    return rc == 0 ? 0 : -EIO;
}

static int copy_path(char dst[128], const char *src, const char *fallback)
{
    int n = snprintf(dst, 128, "%s", src && *src ? src : fallback);
    return n > 0 && n < 128 ? 0 : -ENAMETOOLONG;
}

int fh_bgm_vmm_wire_init(struct fh_bgm_vmm_wire *w, uint32_t bytes,
                         uint32_t align, const char *name, const char *zone)
{
    size_t n;
    if (!w || !bytes || align != 0x400u || !name || !*name || !zone)
        return -EINVAL;
    n = strlen(name);
    if (n >= sizeof(w->name) || strlen(zone) >= sizeof(w->zone))
        return -ENAMETOOLONG;
    memset(w, 0, sizeof(*w));
    w->align = align;
    w->bytes = bytes;
    memcpy(w->name, name, n + 1);
    memcpy(w->zone, zone, strlen(zone) + 1);
    return 0;
}

int fh_bgm_vmm_wire_result(const struct fh_bgm_vmm_wire *w,
                           struct fh_bgm_block *out)
{
    if (!w || !out || !w->phys_lo || w->phys_hi || !w->user_va || !w->bytes)
        return -EPROTO;
    out->phys = w->phys_lo;
    out->user = (uintptr_t)w->user_va;
    out->bytes = w->bytes;
    return 0;
}

static int linux_ioctl(void *opaque, unsigned long req, void *arg, uint32_t *raw)
{
    struct fh_bgm_linux *l = opaque;
    int rc;
    if (!l || l->bgm_fd < 0)
        return -EBADF;
    errno = 0;
    rc = l->sys.ioctl_fn(l->sys.opaque, l->bgm_fd, req, arg);
    if (raw)
        *raw = (uint32_t)rc;
    if (rc < 0)
        return errno ? -errno : -EIO;
    if (rc > 0)
        return -EIO;
    return 0;
}

static int linux_alloc(void *opaque, uint32_t bytes, uint32_t align,
                       struct fh_bgm_block *out)
{
    struct fh_bgm_linux *l = opaque;
    int rc;
    if (!l || !out || l->vmm_fd < 0 || l->vmm_live ||
        l->vmm_recovery_required || l->vmm_release_unknown)
        return -EINVAL;
    memset(out, 0, sizeof(*out));

    /* RECONFIRMED: BGM owns a dedicated VMM fd, 0x400 alignment, allocation
     * name "bgm", empty zone, and exact MEM_QUERY byte count. */
    rc = fh_bgm_vmm_wire_init(&l->vmm_wire, bytes, align, "bgm", "");
    if (rc)
        return rc;
    rc = sys_ioctl_zero(l, l->vmm_fd, FH_VMM_ALLOC, &l->vmm_wire);
    if (rc) {
        memset(&l->vmm_wire, 0, sizeof(l->vmm_wire));
        return rc;
    }

    /* ALLOC has succeeded. From this point any failure retains ownership until
     * the dedicated VMM fd is explicitly recovered/final-released. */
    l->vmm_live = 1;
    l->vmm_wire.remap_ctl = 0x103u;
    rc = sys_ioctl_zero(l, l->vmm_fd, FH_VMM_REMAP, &l->vmm_wire);
    if (rc) {
        l->vmm_recovery_required = 1;
        return rc;
    }
    rc = fh_bgm_vmm_wire_result(&l->vmm_wire, out);
    if (rc)
        l->vmm_recovery_required = 1;
    return rc;
}

static int linux_free(void *opaque, struct fh_bgm_block *b)
{
    struct fh_bgm_linux *l = opaque;
    int rc;
    if (!l || !b || l->vmm_fd < 0 || l->vmm_release_unknown)
        return -EINVAL;
    if (!l->vmm_live) {
        if (!b->phys && !b->user && !b->bytes)
            return 0;
        return -EPROTO;
    }
    rc = sys_ioctl_zero(l, l->vmm_fd, FH_VMM_FREE_ALL, &l->vmm_wire);
    if (rc) {
        l->vmm_recovery_required = 1;
        return rc;
    }
    l->vmm_live = 0;
    l->vmm_recovery_required = 0;
    memset(&l->vmm_wire, 0, sizeof(l->vmm_wire));
    memset(b, 0, sizeof(*b));
    return 0;
}

static int linux_backend_needs_recovery(void *opaque)
{
    const struct fh_bgm_linux *l = opaque;
    return l && (l->vmm_recovery_required || l->bgm_release_unknown || l->vmm_release_unknown);
}

static int linux_final_release(void *opaque, struct fh_bgm_block *b)
{
    struct fh_bgm_linux *l = opaque;
    int rc;
    if (!l || !b)
        return -EINVAL;
    if (l->bgm_release_unknown || l->vmm_release_unknown)
        return -EUCLEAN;

    /* NEW: /dev/bgm release is the proven final boundary. Never free VMM first. */
    if (l->bgm_fd >= 0) {
        int fd = l->bgm_fd;
        l->bgm_fd = -1; /* POSIX close errors must not be blindly retried. */
        rc = sys_close(l, fd);
        if (rc) {
            l->bgm_release_unknown = 1;
            return rc;
        }
    }

    if (l->vmm_live) {
        rc = linux_free(l, b);
        if (rc)
            return rc;
    } else {
        memset(b, 0, sizeof(*b));
    }

    if (l->vmm_fd >= 0) {
        int fd = l->vmm_fd;
        l->vmm_fd = -1;
        rc = sys_close(l, fd);
        if (rc) {
            l->vmm_release_unknown = 1;
            return rc;
        }
    }
    return 0;
}

static int linux_reopen(void *opaque)
{
    struct fh_bgm_linux *l = opaque;
    int bgm;
    int vmm;
    int rc;

    if (!l || l->bgm_release_unknown || l->vmm_release_unknown ||
        l->vmm_recovery_required || l->vmm_live)
        return -EUCLEAN;
    if (l->bgm_fd >= 0 || l->vmm_fd >= 0)
        return -EBUSY;

    errno = 0;
    bgm = l->sys.open_fn(l->sys.opaque, l->bgm_path, O_RDWR | O_CLOEXEC);
    if (bgm < 0)
        return errno ? -errno : -EIO;

    errno = 0;
    vmm = l->sys.open_fn(l->sys.opaque, l->vmm_path, O_RDWR | O_CLOEXEC);
    if (vmm < 0) {
        int e = errno ? errno : EIO;
        rc = sys_close(l, bgm);
        if (rc) {
            l->bgm_release_unknown = 1;
            return rc;
        }
        return -e;
    }

    l->bgm_fd = bgm;
    l->vmm_fd = vmm;
    return 0;
}

int fh_bgm_linux_open_with_sys(struct fh_bgm_linux *l, const char *bgm_path,
                               const char *vmm_path,
                               const struct fh_bgm_linux_sys_ops *sys)
{
    int rc;
    if (!l || !sys || !sys->open_fn || !sys->close_fn || !sys->ioctl_fn)
        return -EINVAL;
    memset(l, 0, sizeof(*l));
    l->bgm_fd = -1;
    l->vmm_fd = -1;
    l->sys = *sys;
    rc = copy_path(l->bgm_path, bgm_path, "/dev/bgm");
    if (rc)
        return rc;
    rc = copy_path(l->vmm_path, vmm_path, "/dev/vmm_userdev");
    if (rc)
        return rc;
    return linux_reopen(l);
}

int fh_bgm_linux_open(struct fh_bgm_linux *l, const char *bgm_path,
                      const char *vmm_path)
{
    return fh_bgm_linux_open_with_sys(l, bgm_path, vmm_path, &default_sys);
}

int fh_bgm_linux_close_idle(struct fh_bgm_linux *l)
{
    struct fh_bgm_block empty = {0};
    if (!l || l->vmm_live)
        return -EBUSY;
    return linux_final_release(l, &empty);
}

void fh_bgm_linux_ops(struct fh_bgm_linux *l, struct fh_bgm_ops *ops)
{
    if (!ops)
        return;
    memset(ops, 0, sizeof(*ops));
    ops->alloc = linux_alloc;
    ops->free = linux_free;
    ops->ioctl = linux_ioctl;
    ops->final_release = linux_final_release;
    ops->reopen = linux_reopen;
    ops->backend_needs_recovery = linux_backend_needs_recovery;
    ops->opaque = l;
}
