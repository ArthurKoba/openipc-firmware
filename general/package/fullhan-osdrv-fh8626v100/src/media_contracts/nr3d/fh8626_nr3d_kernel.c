#include "fh8626_nr3d_kernel.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

int fh_nr3d_kernel_get(struct fh_nr3d_kernel *k, struct fh_nr3d_driver_config *out)
{
    if (!k || !k->ioctl || !out) return -EINVAL;
    memset(out, 0, sizeof(*out));
    return k->ioctl(k->opaque, FH_ISP_NR3D_QUERY, out);
}

int fh_nr3d_kernel_set_verified(struct fh_nr3d_kernel *k, int enabled,
                                struct fh_nr3d_driver_config *readback)
{
    struct fh_nr3d_driver_config cfg;
    FILE *f;
    const char *path;
    int rc = 0;

    if (!k || !k->ioctl) return -EINVAL;
    path = k->proc_path && *k->proc_path ? k->proc_path : "/proc/driver/isp";
    f = fopen(path, "w");
    if (!f) return -errno;

    if (fprintf(f, "nr3d_%s\n", enabled ? "on" : "off") < 0) rc = -EIO;
    if (fclose(f) && !rc) rc = -errno;
    if (rc) return rc;

    rc = fh_nr3d_kernel_get(k, &cfg);
    if (rc) return rc;
    if ((cfg.mode != 0) != (enabled != 0)) return -EIO;
    if (readback) *readback = cfg;
    return 0;
}
