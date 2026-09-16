#ifndef FH8626_GEOMETRY_LINUX_H
#define FH8626_GEOMETRY_LINUX_H
#include "fh8626_geometry.h"
/* Borrowed fd and owner hooks; this adapter never opens/closes/allocates. */
struct fhg_linux_context {
    int isp_fd;
    void *owner;
    int (*enter)(void *, const struct fhg_plan *, enum fhg_phase);
    void (*leave)(void *);
};
int fhg_linux_make_ops(struct fhg_linux_context *, struct fhg_ops *);
#endif
