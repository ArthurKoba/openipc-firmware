#include "fh8626_geometry_linux.h"
#include <errno.h>
#include <sys/ioctl.h>
static int linux_enter(void *p,const struct fhg_plan *plan,enum fhg_phase phase)
{
    struct fhg_linux_context *c=p;
    return c->enter(c->owner,plan,phase);
}
static void linux_leave(void *p)
{
    struct fhg_linux_context *c=p;c->leave(c->owner);
}
static int linux_control(void *p,uint32_t request,void *payload,size_t size,
                         uint32_t *driver,int *error)
{
    struct fhg_linux_context *c=p;
    int rc;
    if(!payload || !size || size!=fhg_wire_size(request))return -EINVAL;
    errno=0;
    rc=ioctl(c->isp_fd,(unsigned long)request,payload);
    *driver=(uint32_t)rc; *error=rc==-1?errno:0;
    /* Vendor negative results outside Linux's errno range are raw results.
     * Never retry a setter, including EINTR/copy-back failures. */
    return rc==-1 ? -1 : 0;
}
int fhg_linux_make_ops(struct fhg_linux_context *c,struct fhg_ops *out)
{
    const uint32_t endian=1;
    if(!c || !out || c->isp_fd<0 || !c->enter || !c->leave)return -EINVAL;
    if(sizeof(void*)!=4 || *(const unsigned char*)&endian!=1)return -ENOTSUP;
    *out=(struct fhg_ops){c,linux_enter,linux_leave,linux_control};
    return 0;
}
