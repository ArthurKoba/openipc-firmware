#include "fh8626_bgm_linux.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

struct fake_sys {
    int next_fd;
    int open_call;
    int close_call;
    int ioctl_call;
    int fail_open_call;
    int fail_close_fd;
    unsigned long fail_req;
    int bgm_fd;
    int vmm_fd;
    int free_calls;
    int alloc_calls;
    int remap_calls;
};

static int f_open(void *opaque, const char *path, int flags)
{
    struct fake_sys *f = opaque;
    (void)flags;
    f->open_call++;
    if (f->fail_open_call == f->open_call) {
        errno = ENOENT;
        return -1;
    }
    if (strstr(path, "vmm")) {
        f->vmm_fd = f->next_fd++;
        return f->vmm_fd;
    }
    f->bgm_fd = f->next_fd++;
    return f->bgm_fd;
}

static int f_close(void *opaque, int fd)
{
    struct fake_sys *f = opaque;
    f->close_call++;
    if (f->fail_close_fd == fd) {
        errno = EIO;
        return -1;
    }
    return 0;
}

static int f_ioctl(void *opaque, int fd, unsigned long req, void *arg)
{
    struct fake_sys *f = opaque;
    struct fh_bgm_vmm_wire *w;
    f->ioctl_call++;
    if (req == f->fail_req) {
        errno = EIO;
        return -1;
    }
    if (fd == f->vmm_fd) {
        w = arg;
        if (req == FH_VMM_ALLOC) {
            f->alloc_calls++;
            assert(w->bytes == 0x4101u);
            assert(w->align == 0x400u);
            assert(!strcmp(w->name, "bgm"));
            assert(w->zone[0] == '\0');
            w->phys_lo = 0x12340000u;
            w->phys_hi = 0;
        } else if (req == FH_VMM_REMAP) {
            f->remap_calls++;
            assert(w->remap_ctl == 0x103u);
            w->user_va = 0x34560000u;
        } else if (req == FH_VMM_FREE_ALL) {
            f->free_calls++;
        }
    }
    return 0;
}

static struct fh_bgm_linux_sys_ops sysops(struct fake_sys *f)
{
    struct fh_bgm_linux_sys_ops s = { f_open, f_close, f_ioctl, f };
    return s;
}

static void reset_fake(struct fake_sys *f)
{
    memset(f, 0, sizeof(*f));
    f->next_fd = 10;
    f->fail_close_fd = -1;
}

int main(void)
{
    struct fh_bgm_vmm_wire w;
    struct fh_bgm_block b = {0};
    struct fh_bgm_linux l;
    struct fh_bgm_ops ops;
    struct fh_bgm_linux_sys_ops s;
    struct fake_sys f;

    /* Exact wire itself. */
    assert(!fh_bgm_vmm_wire_init(&w, 0x4101u, 0x400u, "bgm", ""));
    assert(w.align == 0x400u);
    assert(w.bytes == 0x4101u);
    assert(!strcmp(w.name, "bgm"));
    assert(w.zone[0] == 0);
    assert(fh_bgm_vmm_wire_init(&w, 1, 0x1000, "bgm", "") == -EINVAL);
    assert(fh_bgm_vmm_wire_result(&w, &b) == -EPROTO);
    w.phys_lo = 0x12340000u;
    w.phys_hi = 0;
    w.user_va = 0x34560000u;
    assert(!fh_bgm_vmm_wire_result(&w, &b));
    assert(b.phys == w.phys_lo && b.user == (uintptr_t)w.user_va && b.bytes == 0x4101u);

    /* Dedicated opens: first /dev/bgm, then a distinct /dev/vmm_userdev. */
    reset_fake(&f);
    s = sysops(&f);
    assert(!fh_bgm_linux_open_with_sys(&l, "/dev/bgm", "/dev/vmm_userdev", &s));
    assert(l.bgm_fd == 10 && l.vmm_fd == 11 && l.bgm_fd != l.vmm_fd);
    fh_bgm_linux_ops(&l, &ops);
    memset(&b, 0, sizeof(b));
    assert(!ops.alloc(ops.opaque, 0x4101u, 0x400u, &b));
    assert(f.alloc_calls == 1 && f.remap_calls == 1);
    assert(l.vmm_live && !ops.backend_needs_recovery(ops.opaque));
    assert(!ops.free(ops.opaque, &b));
    assert(f.free_calls == 1 && !l.vmm_live);
    assert(!fh_bgm_linux_close_idle(&l));

    /* BGM open failure never opens VMM. */
    reset_fake(&f); f.fail_open_call = 1; s = sysops(&f);
    assert(fh_bgm_linux_open_with_sys(&l, "/dev/bgm", "/dev/vmm_userdev", &s) == -ENOENT);
    assert(f.open_call == 1);

    /* VMM open failure closes the already-open BGM fd. */
    reset_fake(&f); f.fail_open_call = 2; s = sysops(&f);
    assert(fh_bgm_linux_open_with_sys(&l, "/dev/bgm", "/dev/vmm_userdev", &s) == -ENOENT);
    assert(f.open_call == 2 && f.close_call == 1);

    /* ALLOC failure leaves no live ownership. */
    reset_fake(&f); s = sysops(&f);
    assert(!fh_bgm_linux_open_with_sys(&l, "/dev/bgm", "/dev/vmm_userdev", &s));
    fh_bgm_linux_ops(&l, &ops);
    f.fail_req = FH_VMM_ALLOC;
    assert(ops.alloc(ops.opaque, 0x4101u, 0x400u, &b) == -EIO);
    assert(!l.vmm_live && !ops.backend_needs_recovery(ops.opaque));
    f.fail_req = 0;
    assert(!fh_bgm_linux_close_idle(&l));

    /* REMAP failure happens after ALLOC: retained dedicated ownership is
     * surfaced as recovery-required instead of a clean allocation failure. */
    reset_fake(&f); s = sysops(&f);
    assert(!fh_bgm_linux_open_with_sys(&l, "/dev/bgm", "/dev/vmm_userdev", &s));
    fh_bgm_linux_ops(&l, &ops);
    f.fail_req = FH_VMM_REMAP;
    memset(&b, 0, sizeof(b));
    assert(ops.alloc(ops.opaque, 0x4101u, 0x400u, &b) == -EIO);
    assert(l.vmm_live && ops.backend_needs_recovery(ops.opaque));
    f.fail_req = 0;
    assert(!ops.final_release(ops.opaque, &b));
    assert(f.free_calls == 1);

    /* FREE failure preserves the allocation for recovery. */
    reset_fake(&f); s = sysops(&f);
    assert(!fh_bgm_linux_open_with_sys(&l, "/dev/bgm", "/dev/vmm_userdev", &s));
    fh_bgm_linux_ops(&l, &ops);
    assert(!ops.alloc(ops.opaque, 0x4101u, 0x400u, &b));
    f.fail_req = FH_VMM_FREE_ALL;
    assert(ops.free(ops.opaque, &b) == -EIO);
    assert(l.vmm_live && ops.backend_needs_recovery(ops.opaque));
    f.fail_req = 0;
    assert(!ops.final_release(ops.opaque, &b));

    /* Most important shutdown ordering negative: if /dev/bgm release is
     * unknown, VMM FREE must not be issued at all. */
    reset_fake(&f); s = sysops(&f);
    assert(!fh_bgm_linux_open_with_sys(&l, "/dev/bgm", "/dev/vmm_userdev", &s));
    fh_bgm_linux_ops(&l, &ops);
    assert(!ops.alloc(ops.opaque, 0x4101u, 0x400u, &b));
    f.fail_close_fd = l.bgm_fd;
    assert(ops.final_release(ops.opaque, &b) == -EIO);
    assert(l.bgm_release_unknown && l.vmm_live);
    assert(f.free_calls == 0);
    assert(ops.backend_needs_recovery(ops.opaque));
    assert(ops.reopen(ops.opaque) == -EUCLEAN);


    /* Dedicated VMM close failure is also an unknown owner boundary. */
    reset_fake(&f); s = sysops(&f);
    assert(!fh_bgm_linux_open_with_sys(&l, "/dev/bgm", "/dev/vmm_userdev", &s));
    fh_bgm_linux_ops(&l, &ops);
    f.fail_close_fd = l.vmm_fd;
    assert(fh_bgm_linux_close_idle(&l) == -EIO);
    assert(l.vmm_release_unknown && ops.backend_needs_recovery(ops.opaque));
    assert(ops.reopen(ops.opaque) == -EUCLEAN);

    puts("BGM dedicated VMM open/alloc/remap/free/release negative lifecycle: PASS");
    return 0;
}
