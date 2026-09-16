#include "fh8626_geometry.h"
#include "fh8626_geometry_linux.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
struct mock {int locked,calls,fail,transport,guard,bad_geometry;uint32_t need;struct fhg_plan plan;};
static int enter(void *p,const struct fhg_plan *plan,enum fhg_phase phase)
{struct mock *m=p;assert(!m->locked);assert(phase==1 || phase==2);if(m->guard)return -EBUSY;m->locked=1;m->plan=*plan;return 0;}
static void leave(void *p){struct mock *m=p;assert(m->locked);m->locked=0;}
static int control(void *p,uint32_t req,void *wire,size_t size,uint32_t *drv,int *err)
{
 struct mock *m=p;assert(m->locked);assert(size==fhg_wire_size(req));m->calls++;
 if(m->calls==m->fail){*drv=0x80094001;*err=EINTR;return m->transport?-1:0;}
 if(req==FHG_IOCTL_GET_VI){struct fhg_wire_vi *w=wire;w->width=1280;w->height=720;}
 else if(req==FHG_IOCTL_QUERY_MEMORY){struct fhg_wire_query *w=wire;assert(w->width==m->plan.capacity_width);assert(w->height==m->plan.capacity_height);w->bytes=m->need;}
 else if(req==FHG_IOCTL_GET_GEOMETRY){struct fhg_wire_geometry *w=wire;w->width=m->plan.surface_width;w->height=m->plan.surface_height+m->bad_geometry;}
 else if(req==FHG_IOCTL_SET_MEMORY){struct fhg_wire_memory *w=wire;assert(w->bytes>=m->need);assert(w->max_height==m->plan.capacity_height);}
 else if(req==FHG_IOCTL_SET_GEOMETRY){struct fhg_wire_geometry *w=wire;assert(w->height==m->plan.request.visible_height);}
 else if(req==FHG_IOCTL_SET_COEFF){struct fhg_wire_coeff *w=wire;assert(w->selector==(uint32_t)m->plan.request.coefficient);}
 else assert(0);
 return 0;
}
int main(void)
{
 struct fhg_request r=FHG_REQUEST_INITIALIZER;
 struct fhg_plan p;
 struct fhg_memory memory={0x10000000,0x20000000,0x800000};
 struct fhg_error e;
 r.native_width=1280;r.native_height=720;
 const uint32_t vectors[][6]={{640,360,368,384,0x200000,0x1f4dea},{1280,720,720,736,0x100000,0x100000},{1920,1080,1088,1088,0xaaaab,0xa9697}};
 for(unsigned i=0;i<3;i++){
  r.visible_width=vectors[i][0];r.visible_height=vectors[i][1];
  assert(!fhg_make_plan(&r,&p));assert(p.surface_height==vectors[i][2]);assert(p.allocation_height==vectors[i][3]);assert(p.scaler_step_x==vectors[i][4]);assert(p.scaler_step_y==vectors[i][5]);
 }
 r.coefficient=13;
 for(int transport=0;transport<2;transport++)for(int fail=0;fail<=6;fail++){
  struct mock m={0};m.need=0x700000;m.fail=fail;m.transport=transport;
  struct fhg_ops o={&m,enter,leave,control};struct fhg_session s={0};
  int rc=fhg_configure_new_channel(&o,&r,&memory,&s,&e);
  assert(!m.locked);
  if(fail){assert(rc==(transport?FHG_E_TRANSPORT:FHG_E_DRIVER));assert(e.driver_result==0x80094001);assert(m.calls==fail);
   assert(s.state==(fail<3?FHG_STATE_FRESH:FHG_STATE_RECOVERY_REQUIRED));
   if(fail>=3)assert(s.retained_memory.physical==memory.physical);
  }else{assert(!rc);assert(s.state==FHG_STATE_CONFIGURED_NOT_STREAMING);assert(s.coefficient_write_acknowledged);assert(s.geometry_driver_readback_matches);}
  if(s.state!=FHG_STATE_FRESH){int count=m.calls;assert(fhg_configure_new_channel(&o,&r,&memory,&s,&e)==FHG_E_STATE);assert(m.calls==count);}
 }
 {struct mock m={0};m.need=0x700000;struct fhg_ops o={&m,enter,leave,control};struct fhg_requirements q;struct fhg_session s={0};
  assert(!fhg_query_requirements(&o,&r,&q,&e));assert(q.bytes==m.need);m.need=0x900000;
  assert(fhg_configure_new_channel(&o,&r,&memory,&s,&e)==FHG_E_MEMORY);assert(s.state==FHG_STATE_FRESH);assert(m.calls==4);
  m.need=0x700000;m.bad_geometry=1;
  assert(fhg_configure_new_channel(&o,&r,&memory,&s,&e)==FHG_E_READBACK);assert(s.state==FHG_STATE_RECOVERY_REQUIRED);
 }
 {struct mock m={0};m.need=1024;m.guard=1;struct fhg_ops o={&m,enter,leave,control};struct fhg_session s={0};
  assert(fhg_configure_new_channel(&o,&r,&memory,&s,&e)==FHG_E_GUARD);assert(!m.calls && !m.locked);
  m.guard=0;memory.physical=0xfffffc00;
  assert(fhg_configure_new_channel(&o,&r,&memory,&s,&e)==FHG_E_MEMORY);assert(s.state==FHG_STATE_FRESH);
 }
 r.channel=2;assert(fhg_make_plan(&r,&p)==FHG_E_UNSUPPORTED);r.channel=0;
 r.coefficient=16;assert(fhg_make_plan(&r,&p)==FHG_E_RANGE);r.coefficient=-1;
 r.native_height=2048;r.visible_height=32;assert(fhg_make_plan(&r,&p)==FHG_E_RANGE);
 r.native_height=720;r.visible_height=2048;r.channel=1;assert(fhg_make_plan(&r,&p)==FHG_E_RANGE);
 r.channel=0;r.visible_height=1081;assert(fhg_make_plan(&r,&p)==FHG_E_RANGE);
 r.visible_height=1080;r.capacity_width=640;r.capacity_height=360;assert(fhg_make_plan(&r,&p)==FHG_E_MEMORY);
 r.capacity_height=0;assert(fhg_make_plan(&r,&p)==FHG_E_ARGUMENT);
 {struct fhg_linux_context c={3,NULL,enter,leave};struct fhg_ops o;
  if(sizeof(void*)!=4)assert(fhg_linux_make_ops(&c,&o)==-ENOTSUP);
 }
 puts("geometry vectors / failures / requery / retention / ABI: PASS");return 0;
}
