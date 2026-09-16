#define _GNU_SOURCE
#include "fh8626_nr3d_lifecycle.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct fake {
    uint32_t mode;
    int fail_ioctl;
};

static int io(void *opaque, unsigned long req, void *arg)
{
    struct fake *f = opaque;
    assert(req == FH_ISP_NR3D_QUERY);
    if (f->fail_ioctl)
        return f->fail_ioctl;
    ((struct fh_nr3d_driver_config *)arg)->mode = f->mode;
    return 0;
}

static void read_proc(const char *path, char *buf, size_t size)
{
    FILE *f = fopen(path, "r");
    assert(f);
    assert(fgets(buf, (int)size, f));
    fclose(f);
}

int main(void)
{
    char path[] = "/tmp/fh_nr3d_lifecycle_XXXXXX";
    char buf[32];
    int fd = mkstemp(path);
    struct fake f;
    struct fh_nr3d_kernel k;
    struct fh_nr3d_lifecycle l;

    assert(fd >= 0);
    close(fd);
    memset(&f, 0, sizeof(f));
    k.ioctl = io;
    k.opaque = &f;
    k.proc_path = path;

    assert(fh_nr3d_lifecycle_init(&l, &k) == 0);
    f.mode = 1;
    assert(fh_nr3d_lifecycle_cold_start(&l, 1) == 0);
    assert(l.state == FH_NR3D_COLD_ON);
    memset(buf, 0, sizeof(buf));
    read_proc(path, buf, sizeof(buf));
    assert(!strcmp(buf, "nr3d_on\n"));

    /* Controlled ON->OFF selector change is allowed. */
    f.mode = 0;
    assert(fh_nr3d_lifecycle_hot_disable(&l) == 0);
    assert(l.state == FH_NR3D_HOT_DISABLED);
    assert(fh_nr3d_lifecycle_needs_restart(&l));
    memset(buf, 0, sizeof(buf));
    read_proc(path, buf, sizeof(buf));
    assert(!strcmp(buf, "nr3d_off\n"));

    /* OFF->ON never writes nr3d_on; it becomes restart-required. */
    assert(fh_nr3d_lifecycle_request_hot_enable(&l) == FH_NR3D_ACTION_RESTART_REQUIRED);
    assert(l.state == FH_NR3D_RESTART_REQUIRED);
    memset(buf, 0, sizeof(buf));
    read_proc(path, buf, sizeof(buf));
    assert(!strcmp(buf, "nr3d_off\n"));

    /* An external restart starts a new software epoch; no hardware reset is
     * claimed by this marker itself. */
    assert(fh_nr3d_lifecycle_begin_after_external_restart(&l) == 0);
    assert(l.state == FH_NR3D_FRESH);
    f.mode = 1;
    assert(fh_nr3d_lifecycle_cold_start(&l, 1) == 0);
    assert(fh_nr3d_lifecycle_request_history_boundary(&l) == FH_NR3D_ACTION_RESTART_REQUIRED);
    assert(l.state == FH_NR3D_RESTART_REQUIRED);

    /* Readback mismatch is completion-unknown -> poison/recovery. */
    assert(fh_nr3d_lifecycle_begin_after_external_restart(&l) == 0);
    f.mode = 0;
    assert(fh_nr3d_lifecycle_cold_start(&l, 1) == -EIO);
    assert(l.state == FH_NR3D_POISONED);
    assert(fh_nr3d_lifecycle_needs_recovery(&l));
    assert(fh_nr3d_lifecycle_request_hot_enable(&l) == FH_NR3D_ACTION_RECOVERY_REQUIRED);

    /* Recovery requires an externally completed restart; then cold OFF works. */
    assert(fh_nr3d_lifecycle_begin_after_external_restart(&l) == 0);
    f.mode = 0;
    assert(fh_nr3d_lifecycle_cold_start(&l, 0) == 0);
    assert(l.state == FH_NR3D_COLD_OFF);
    assert(fh_nr3d_lifecycle_request_history_boundary(&l) == FH_NR3D_ACTION_OK);
    assert(fh_nr3d_lifecycle_request_hot_enable(&l) == FH_NR3D_ACTION_RESTART_REQUIRED);

    fh_nr3d_lifecycle_destroy(&l);
    unlink(path);
    puts("NR3D cold/restart lifecycle and no-hot-reenable policy: PASS");
    return 0;
}
