#include "fh8626_ae_runtime.h"
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>

struct events { unsigned n,kind[24]; uint32_t value[24]; int fail; };
static struct events e;
static struct fh8626_ae_runtime *active;
static uint32_t intt_read=100,gain_read=64,timing_fps=25;
static uint16_t effective=995;
static int getter_rc,timing_rc;
static int expect_staged_gain;
static void event(unsigned kind,uint32_t value) {
 assert(e.n<24);e.kind[e.n]=kind;e.value[e.n++]=value;
}
static int get_intt(uint32_t *v){event(10,intt_read);*v=intt_read;return getter_rc;}
static int get_gain(uint32_t *v){
 if(expect_staged_gain)assert(active->queue[1].dirty==1);
 event(11,gain_read);*v=gain_read;return getter_rc;
}
static int get_effective(void *p,uint16_t *v){(void)p;*v=effective;return 0;}
static int get_timing(void *p,uint32_t t[4]){
 (void)p;memset(t,0,4*sizeof(*t));t[2]=timing_fps;return timing_rc;
}
static int stage(void *p,unsigned pair,uint32_t v){
 (void)p;event(pair,v);return e.fail;
}
static void put16(uint8_t *p,unsigned off,uint16_t v){memcpy(p+off,&v,2);}
static uint16_t get16(uint8_t *p,unsigned off){uint16_t v;memcpy(&v,p+off,2);return v;}
static void setup(struct fh8626_ae_runtime *a,uint8_t *c,struct fh_sensor_gc1054 *s){
 memset(c,0,0xa80);memset(&e,0,sizeof(e));
 put16(c,0x1a,1000);put16(c,0x38,995);put16(c,0x36,512);
 put16(c,0x34,256);put16(c,0x40,128);put16(c,0x42,96);
 put16(c,0x5a,100);put16(c,0x5e,64);put16(c,0x5c,64);
 c[0x2c]=3;c[0x2f]=0x80;c[0x3b]=0x10;c[0x3c]=1;c[0xa4c]=50;
 fh8626_ae_runtime_init_passive(a,c,NULL,s,NULL,NULL,get_timing,NULL);
 a->get_effective_intt=get_effective;a->stage=stage;
 a->measured=1000;a->target=2000;a->error=-1000;
 active=a;effective=995;intt_read=100;gain_read=64;timing_fps=25;
 getter_rc=timing_rc=expect_staged_gain=0;
}
int main(void){
 struct fh8626_ae_runtime a;struct fh_sensor_gc1054 s={0};
 uint8_t c[0xa80],cb[FH_SENSOR_CB_SIZE]={0};void *p;uint32_t out,slot;
 p=(void *)(uintptr_t)get_intt;memcpy(cb+0x18,&p,sizeof(p));
 p=(void *)(uintptr_t)get_gain;memcpy(cb+0x0c,&p,sizeof(p));s.cb=cb;
 /* C7058 immediate exposure -> actual readback -> deferred Q8; context and
  * delayed Q8 are snapshots, not sensor-request mirrors. */
 setup(&a,c,&s);
 assert(fh8626_ae_runtime_c7058_intt(&a,1.25f,256,&out)==0);
 assert(out==125&&e.n==2&&e.kind[0]==0&&e.value[0]==125&&e.kind[1]==10);
 assert(a.q8_aux==320&&a.queue[3].dirty==1&&a.queue[3].value==320);
 assert(get16(c,0x5a)==100&&get16(c,0x5e)==64);
 setup(&a,c,&s);c[0x3a]=0x40;a.q8_alt=384;
 assert(fh8626_ae_runtime_c7058_intt(&a,1.25f,384,&out)==0);
 assert(out==187&&a.q8_aux==480&&a.queue[3].value==384);
 setup(&a,c,&s);getter_rc=-EIO;e.fail=-ENOSPC;intt_read=200;
 assert(fh8626_ae_runtime_c7058_intt(&a,1.25f,256,&out)==-ENOSPC);
 assert(e.n==2&&a.queue[3].dirty==1&&a.queue[3].value==256);
 setup(&a,c,&s);c[0x2c]=1;
 assert(fh8626_ae_runtime_c7058_intt(&a,1.25f,256,&out)==0);
 assert(e.n==1&&a.queue[3].dirty==0);
 setup(&a,c,&s);intt_read=0;
 assert(fh8626_ae_runtime_c7058_intt(&a,1.25f,256,&out)==-ERANGE);
 assert(e.n==2&&a.queue[3].dirty==0);

 /* C72A0 no immediate callbacks: queue0 plus queue3, even when readback
  * callbacks would return different exposure or fail. */
 setup(&a,c,&s);put16(c,0x5a,600);intt_read=10;getter_rc=-EIO;
 assert(fh8626_ae_runtime_c72a0_antiflick(&a,1.25f,&out)==0);
 assert(a.anti_flicker_quantum==250&&out==750&&a.q8_aux==256);
 assert(e.n==0&&a.queue[0].value==750&&a.queue[3].value==256);
 assert(get16(c,0x5a)==600);
 setup(&a,c,&s);c[0xa4c]=60;
 assert(fh8626_ae_runtime_c72a0_antiflick(&a,1.0f,&out)==0);
 assert(a.anti_flicker_quantum==208);
 /* Full Q8 boundary: 250.5 lines must not take the <=250 fallback to q=1. */
 setup(&a,c,&s);put16(c,0x5a,167);c[0x3a]=0x40;a.q8_alt=384;
 assert(fh8626_ae_runtime_c72a0_antiflick(&a,1.0f,&out)==0);
 assert(out==250&&a.queue[3].value==384&&a.q8_aux==257);
 setup(&a,c,&s);put16(c,0x5a,100);c[0xa4f]=0x80;
 assert(fh8626_ae_runtime_c72a0_antiflick(&a,1.0f,&out)==0&&out==250);
 setup(&a,c,&s);
 assert(fh8626_ae_runtime_c72a0_antiflick(&a,1.0f,&out)==0&&out==100);
 /* Stock C71C8 wraps MUL before UMULL. With selector0, the widened value
  * would be12500; actual low32 result is3910. */
 setup(&a,c,&s);c[0xa4c]=0;
 assert(fh8626_ae_runtime_c72a0_antiflick(&a,1.0f,&out)==0);
 assert(a.anti_flicker_quantum==3910);
 setup(&a,c,&s);timing_rc=-EIO;
 assert(fh8626_ae_runtime_c72a0_antiflick(&a,1.0f,&out)==-EIO);
 assert(e.n==0&&a.queue[0].dirty==0&&a.queue[3].dirty==0);

 /* Full negative dispatch uses C6D9C and branches by ctx3F, not gain mode.
  * Pair callbacks below log applied values; getter sees queued gain first. */
 setup(&a,c,&s);expect_staged_gain=1;put16(c,0x5a,995);
 assert(fh8626_ae_runtime_c883c_day(&a,1.25f)==0);
 assert(a.action_code==4&&e.n==3&&e.kind[0]==11&&e.kind[1]==1&&e.value[1]==80);
 assert(e.kind[2]==3&&e.value[2]==320&&a.q8_alt==320);
 setup(&a,c,&s);expect_staged_gain=1;put16(c,0x5a,995);c[0x3a]=0x80;a.q8_alt=384;
 assert(fh8626_ae_runtime_c883c_day(&a,1.0f)==0);
 assert(e.n==3&&e.value[1]==96&&e.value[2]==384&&a.q8_alt==384);
 setup(&a,c,&s);a.measured=3000;put16(c,0x5e,128);
 assert(fh8626_ae_runtime_c883c_day(&a,0.75f)==0);
 assert(a.action_code==1&&e.kind[0]==0&&e.value[0]==75);
 setup(&a,c,&s);c[0x3f]=2;put16(c,0x5a,995);put16(c,0x5e,128);
 assert(fh8626_ae_runtime_c883c_day(&a,1.25f)==0);
 assert(a.action_code==7&&e.n==3&&e.kind[0]==4&&e.value[0]==2);
 assert(e.kind[1]==0&&e.value[1]==995&&e.kind[2]==4&&e.value[2]==2);
 setup(&a,c,&s);c[0x3f]=2;effective=1995;a.measured=3000;put16(c,0x5a,1995);
 assert(fh8626_ae_runtime_c883c_day(&a,0.25f)==0);
 assert(a.action_code==7&&e.n==3&&e.value[0]==1&&e.value[1]==995);
 setup(&a,c,&s);c[0x3f]=2;effective=1995;a.measured=3000;put16(c,0x5a,1995);
 assert(fh8626_ae_runtime_c883c_day(&a,0.75f)==0);
 assert(a.action_code==7&&e.n==1&&e.kind[0]==0&&e.value[0]==1496);
 setup(&a,c,&s);c[0x2c]=0;c[0x3a]=0x40;a.action_code=1;a.q8_alt=384;
 a.published_action_slot=&slot;slot=99;
 assert(fh8626_ae_runtime_c883c_day(&a,1.0f)==0);
 assert(a.action_code==0&&slot==0&&e.n==1&&e.kind[0]==3&&e.value[0]==384);
 assert(a.q8_alt==256);
 setup(&a,c,&s);c[0x2c]=0;c[0x3a]=0x80;a.action_code=4;a.q8_alt=384;
 assert(fh8626_ae_runtime_c883c_day(&a,1.0f)==0);
 assert(e.n==1&&e.value[0]==384);
 setup(&a,c,&s);c[0x2c]=0x0b;put16(c,0x5a,600);
 assert(fh8626_ae_runtime_c883c_day(&a,1.25f)==0);
 assert(a.action_code==9&&e.n==2&&e.kind[0]==0&&e.value[0]==750&&e.kind[1]==3);
 setup(&a,c,&s);put16(c,0x5a,995);put16(c,0x5e,512);
 assert(fh8626_ae_runtime_c883c_day(&a,1.25f)==0);
 assert(a.action_code==5&&e.n==1&&e.kind[0]==3&&e.value[0]==320);

 /* Error tail flushes an earlier bounds record, preserves first diagnostic,
  * clears dirty, and does not overwrite ctx with sensor state. */
 setup(&a,c,&s);a.queue[1].dirty=1;a.queue[1].value=96;timing_rc=-EIO;
 e.fail=-ENOSPC;
 assert(fh8626_ae_runtime_c883c_day(&a,1.0f)==-EIO);
 assert(e.n==1&&e.kind[0]==1&&a.queue[1].dirty==0&&a.queue[1].value==96);
 setup(&a,c,&s);c[0x2c]=4;
 assert(fh8626_ae_runtime_c883c_day(&a,1.0f)==-EOPNOTSUPP);
 assert(a.action_code==6&&e.n==0);
 /* Iris lower and upper edges, signed error unlike positive absolute error. */
 for(unsigned i=0;i<4;i++){
  setup(&a,c,&s);c[0x2c]=4;a.error=(int32_t[]){10,11,2689,2690}[i];
  fh8626_ae_runtime_set_slow_controller(&a,1.0f,1.0f,0.0f);
  assert(fh8626_ae_runtime_c883c_day(&a,1.0f)==0);
  assert((a.slow_iris==(int32_t[]){-1,0,0,1}[i]));
 }
 setup(&a,c,&s);c[0x2c]=4;a.slow_iris=20;a.error=3000;
 fh8626_ae_runtime_set_slow_controller(&a,1.0f,1.0f,0.0f);
 assert(fh8626_ae_runtime_c883c_day(&a,1.0f)==0&&a.slow_iris==21);
 assert(fh8626_ae_runtime_c883c_day(&a,1.0f)==0&&a.slow_iris==20&&a.action_code==0);
 /* Gate-closed negative delayed Q8 (state2 with equal metric), then a
  * gate-open equal metric must actually dispatch, no invented abs(error)<2. */
 setup(&a,c,&s);a.initialized=1;a.gate.state=2;
 c[0x31]=c[0x32]=10;c[0x33]=5;c[0x3a]=0x40;a.action_code=1;a.q8_alt=384;
 assert(fh8626_ae_runtime_enable_observe(&a,1)==0);
 assert(fh8626_ae_runtime_enable_commit(&a,1)==0);
 assert(fh8626_ae_runtime_step_metric(&a,2000,2000)==0);
 assert(e.n==1&&e.kind[0]==3&&e.value[0]==384);
 setup(&a,c,&s);a.initialized=1;
 assert(fh8626_ae_runtime_enable_observe(&a,1)==0);
 assert(fh8626_ae_runtime_enable_commit(&a,1)==0);
 assert(fh8626_ae_runtime_step_metric(&a,2000,2000)==0);
 assert(a.action_code==1&&e.n==3&&e.kind[0]==0&&e.value[0]==100);
 return 0;
}
