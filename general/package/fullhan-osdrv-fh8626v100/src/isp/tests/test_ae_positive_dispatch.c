#include "fh8626_ae_runtime.h"
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
struct events { unsigned n,pair[8]; uint32_t value[8]; int fail; };
static uint32_t current_intt=100, current_gain=64;
static uint16_t effective=995;
static int get_effective(void *p,uint16_t *v){(void)p;*v=effective;return 0;}
static int get_intt(uint32_t *v){*v=current_intt;return 0;}
static int get_gain(uint32_t *v){*v=current_gain;return 0;}
static int stage(void *p,unsigned pair,uint32_t value){
 struct events *e=p; assert(e->n<8);e->pair[e->n]=pair;e->value[e->n++]=value;
 return e->fail;
}
static void put16(uint8_t *p,unsigned a,uint16_t v){memcpy(p+a,&v,2);}
static void setup(struct fh8626_ae_runtime *a,uint8_t *c,struct fh_sensor_gc1054 *s,struct events *e){
 memset(c,0,0xa80);memset(e,0,sizeof(*e));
 put16(c,0x1a,1000);put16(c,0x38,995);put16(c,0x36,512);
 put16(c,0x34,256);put16(c,0x40,128);put16(c,0x42,96);
 put16(c,0x5a,100);put16(c,0x5e,64);put16(c,0x5c,64);
 c[0x2c]=3;c[0x3a]=0x30;c[0x3b]=0x10;c[0x3c]=1;
 fh8626_ae_runtime_init_passive(a,c,NULL,s,NULL,NULL,NULL,NULL);
 a->get_effective_intt=get_effective;a->stage=stage;a->commit_opaque=e;
 a->measured=1000;a->target=2000;effective=995;current_intt=100;current_gain=64;
}
int main(void){
 struct fh8626_ae_runtime a;struct fh_sensor_gc1054 s={0};struct events e;
 uint8_t c[0xa80],cb[FH_SENSOR_CB_SIZE]={0};void *p;
 p=(void *)(uintptr_t)get_intt;memcpy(cb+0x18,&p,sizeof(p));
 p=(void *)(uintptr_t)get_gain;memcpy(cb+0x0c,&p,sizeof(p));s.cb=cb;
 setup(&a,c,&s,&e);
 assert(fh8626_ae_runtime_c8134_positive(&a,1.25f)==0);
 assert(a.action_code==1&&e.n==1&&e.pair[0]==0&&e.value[0]==125);
 assert(a.queue[1].dirty==1&&a.queue[1].value==80);
 setup(&a,c,&s,&e);put16(c,0x5a,995);
 assert(fh8626_ae_runtime_c8134_positive(&a,1.25f)==0);
 assert(a.action_code==4&&a.queue[1].value==80&&e.n==0);
 setup(&a,c,&s,&e);put16(c,0x5a,995);put16(c,0x5e,128);
 assert(fh8626_ae_runtime_c8134_positive(&a,1.25f)==0);
 assert(a.action_code==4&&a.queue[1].value==160);
 setup(&a,c,&s,&e);put16(c,0x5a,995);put16(c,0x5e,512);
 assert(fh8626_ae_runtime_c8134_positive(&a,1.25f)==0);
 assert(a.action_code==5&&a.queue[2].dirty==1&&a.queue[2].value==80);
 assert(a.queue[3].dirty==0);
 setup(&a,c,&s,&e);a.measured=3000;put16(c,0x5c,128);
 assert(fh8626_ae_runtime_c8134_positive(&a,0.75f)==0);
 assert(a.action_code==5&&a.queue[2].value==96);
 setup(&a,c,&s,&e);a.measured=3000;put16(c,0x5a,995);put16(c,0x5e,128);
 assert(fh8626_ae_runtime_c8134_positive(&a,0.75f)==0);
 assert(a.action_code==4&&a.queue[1].value==96);
 setup(&a,c,&s,&e);a.measured=3000;put16(c,0x5e,128);
 assert(fh8626_ae_runtime_c8134_positive(&a,0.75f)==0);
 assert(a.action_code==1&&e.value[0]==150&&a.queue[1].value==96);
 /* Timing expansion holds previous-frame integration limit; contraction
  * uses the new frame limit; equal multiplier clamps scaled integration. */
 setup(&a,c,&s,&e);c[0x3f]=2;put16(c,0x5a,995);put16(c,0x5e,128);
 assert(fh8626_ae_runtime_c8134_positive(&a,1.25f)==0);
 assert(a.action_code==7&&e.n==1&&e.pair[0]==4&&e.value[0]==2);
 assert(a.queue[0].value==995&&a.queue[4].dirty==0);
 setup(&a,c,&s,&e);c[0x3f]=2;effective=1995;a.measured=3000;put16(c,0x5a,1995);
 assert(fh8626_ae_runtime_c8134_positive(&a,0.25f)==0);
 assert(a.action_code==7&&e.value[0]==1&&a.queue[0].value==995);
 setup(&a,c,&s,&e);c[0x3f]=2;effective=1995;a.measured=3000;put16(c,0x5a,1995);
 assert(fh8626_ae_runtime_c8134_positive(&a,0.75f)==0);
 assert(a.action_code==7&&e.n==0&&a.queue[0].value==1496);
 /* Distinct dwell: first close epoch actuates, following four hold. */
 setup(&a,c,&s,&e);c[0x32]=10;a.measured=1999;c[0x2c]=0;
 assert(fh8626_ae_runtime_c8134_positive(&a,1.0f)==0&&a.positive_dwell==1);
 c[0x3a]=0x70;a.action_code=1;a.positive_delayed_gain=96;
 for(unsigned i=2;i<=5;i++){
  a.queue[1].dirty=0;
  assert(fh8626_ae_runtime_c8134_positive(&a,1.0f)==0);
  assert(a.positive_dwell==(i==5?0:i));
  assert(a.queue[1].dirty==1&&a.queue[1].value==96);
 }
 /* Exact saturation-helper boundary: stock reaches21, then clamps on next
  * dark pass. Positive iris uses absolute error, reciprocal multiplication. */
 setup(&a,c,&s,&e);c[0x2c]=4;a.slow_iris=20;
 fh8626_ae_runtime_set_slow_controller(&a,3.0f,1.0f,0.0f);
 assert(fh8626_ae_runtime_c8134_positive(&a,1.0f)==0);
 assert(a.action_code==6&&a.slow_iris==21);
 assert(fh8626_ae_runtime_c8134_positive(&a,1.0f)==0);
 assert(a.action_code==0&&a.slow_iris==20);
 setup(&a,c,&s,&e);c[0x2c]=4;a.measured=3000;
 assert(fh8626_ae_runtime_c8134_positive(&a,1.0f)==-EOPNOTSUPP);
 /* Full metric path dispatches positive factor, flushes staged compensation,
  * and preserves snapshot ctx (fresh getters need not replace history). */
 setup(&a,c,&s,&e);a.initialized=1;
 assert(fh8626_ae_runtime_enable_observe(&a,1)==0);
 assert(fh8626_ae_runtime_enable_commit(&a,1)==0);
 assert(fh8626_ae_runtime_step_metric(&a,1000,2000)==0);
 assert(a.action_code==1&&a.last_factor_q12==5120);
 assert(e.n==2&&e.pair[0]==0&&e.value[0]==125&&e.pair[1]==1&&e.value[1]==80);
 setup(&a,c,&s,&e);a.initialized=1;current_intt=200;current_gain=128;
 assert(fh8626_ae_runtime_enable_observe(&a,1)==0);
 assert(fh8626_ae_runtime_enable_commit(&a,1)==0);
 assert(fh8626_ae_runtime_step_metric(&a,1000,2000)==0);
 assert(e.n==2&&e.value[0]==125); /* aligned ctx100, NOT fresh sensor200 */
 assert(e.value[1]==64); /* readback200 is correct only for compensation */
 setup(&a,c,&s,&e);a.initialized=1;
 a.history[58]=-200;a.history[59]=200;
 assert(fh8626_ae_runtime_enable_observe(&a,1)==0);
 assert(fh8626_ae_runtime_enable_commit(&a,1)==0);
 assert(fh8626_ae_runtime_step_metric(&a,1000,2000)==0);
 assert(a.gate.state==2&&e.n==0); /* sign-crossing uses OLD newest59 */
 setup(&a,c,&s,&e);a.initialized=1;c[0x2c]=1;
 assert(fh8626_ae_runtime_enable_observe(&a,1)==0);
 assert(fh8626_ae_runtime_enable_commit(&a,1)==0);
 assert(fh8626_ae_runtime_step_metric(&a,0,2000)==0);
 assert(a.measured==0&&a.action_code==1&&e.n==1&&e.value[0]==995);
 /* Gate-closed C949C positive tail must flush the delayed gain as well. */
 setup(&a,c,&s,&e);a.initialized=1;a.gate.state=2;c[0x31]=c[0x32]=10;c[0x33]=5;
 c[0x3a]=0x70;a.action_code=1;a.positive_delayed_gain=96;
 assert(fh8626_ae_runtime_enable_observe(&a,1)==0);
 assert(fh8626_ae_runtime_enable_commit(&a,1)==0);
 assert(fh8626_ae_runtime_step_metric(&a,2000,2000)==0);
 assert(e.n==1&&e.pair[0]==1&&e.value[0]==96);
 /* C6D9C base provider: numerator wrap32, divisor/result truncation16. */
 { uint32_t mmio[0xaa0/4]={0};uint16_t v=77;
  a.get_effective_intt=NULL;a.isp_mmio=mmio;
  mmio[0xa98/4]=99599;mmio[0xa94/4]=99;
  assert(fh8626_ae_runtime_c6d9c_read_intt(&a,&v)==0&&v==996);
  mmio[0xa94/4]=0xffff;
  assert(fh8626_ae_runtime_c6d9c_read_intt(&a,&v)==-ERANGE&&v==996);
  mmio[0xa94/4]=0;mmio[0xa98/4]=0xffffffff;
  assert(fh8626_ae_runtime_c6d9c_read_intt(&a,&v)==0&&v==0);
 }
 return 0;
}
