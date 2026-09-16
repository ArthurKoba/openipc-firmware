#ifndef FH8626_NR3D_KERNEL_H
#define FH8626_NR3D_KERNEL_H
#include <stddef.h>
#include <stdint.h>
#define FH_ISP_NR3D_QUERY 0x80206926UL
struct fh_nr3d_driver_config { uint32_t mode, opaque[7]; };
_Static_assert(sizeof(struct fh_nr3d_driver_config) == 0x20, "NR3D query wire ABI");
_Static_assert(offsetof(struct fh_nr3d_driver_config, mode) == 0x00, "NR3D selector offset");
typedef int (*fh_nr3d_ioctl_fn)(void *opaque,unsigned long req,void *arg);
struct fh_nr3d_kernel { fh_nr3d_ioctl_fn ioctl; void *opaque; const char *proc_path; };
int fh_nr3d_kernel_get(struct fh_nr3d_kernel *k,struct fh_nr3d_driver_config *out);
int fh_nr3d_kernel_set_verified(struct fh_nr3d_kernel *k,int enabled,struct fh_nr3d_driver_config *readback);
#endif
