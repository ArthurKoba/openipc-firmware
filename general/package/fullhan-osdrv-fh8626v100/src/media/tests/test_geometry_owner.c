#define main unused_owner_main
#define ioctl mock_geometry_ioctl
#include "../fh8626_media_owner_v4_3_0.c"
#undef ioctl
#undef main
#include <assert.h>
#include <stdarg.h>
static uint32_t result=0x80094081u;
static int io_calls;
int mock_geometry_ioctl(int fd,unsigned long request,...)
{
 va_list ap;uint32_t *w;assert(fd==7);assert(request==0xc0f46945UL);
 va_start(ap,request);w=va_arg(ap,uint32_t*);va_end(ap);
 assert(w[0]<3);io_calls++;return (int)result;
}
int main(void)
{
 struct app a={0};struct fhg_plan p={0};
 assert(owner_ioctl_error((int)0x80094001,0)==-EIO);
 assert(owner_ioctl_error(-1,EINTR)==-EINTR);
 assert(owner_ioctl_error(0,EINVAL)==0);
 pthread_mutex_init(&a.control_mu,NULL);
 a.owner_lock=6;a.isp=7;a.geometry_startup_ready=1;a.geometry_exclusive_open=1;
 p.request.native_width=1280;p.request.native_height=720;
 assert(!geometry_enter(&a,&p,FHG_PHASE_QUERY));assert(io_calls==3);
 assert(pthread_mutex_trylock(&a.control_mu)==EBUSY);geometry_leave(&a);
 assert(geometry_enter(&a,&p,FHG_PHASE_CONFIGURE_NEW)==-EINVAL);
 a.vch=(struct mem3){0x10000000,0x20000000,4096};
 assert(!geometry_enter(&a,&p,FHG_PHASE_CONFIGURE_NEW));geometry_leave(&a);
 result=0;assert(geometry_enter(&a,&p,FHG_PHASE_QUERY)==-EBUSY);
 result=0xffffffff;assert(geometry_enter(&a,&p,FHG_PHASE_QUERY)==-EBUSY);
 result=0x80094081;
 a.geometry.state=FHG_STATE_RECOVERY_REQUIRED;int before=io_calls;
 assert(geometry_enter(&a,&p,FHG_PHASE_QUERY)==-EBUSY);assert(io_calls==before);
 a.geometry.state=FHG_STATE_FRESH;a.running=1;
 assert(geometry_enter(&a,&p,FHG_PHASE_QUERY)==-EBUSY);
 a.running=0;a.geometry_exclusive_open=0;
 assert(geometry_enter(&a,&p,FHG_PHASE_QUERY)==-EBUSY);
 assert(!pthread_mutex_trylock(&a.control_mu));pthread_mutex_unlock(&a.control_mu);
 pthread_mutex_destroy(&a.control_mu);puts("actual owner geometry guard / lock / existing allocation rejection: PASS");return 0;
}
