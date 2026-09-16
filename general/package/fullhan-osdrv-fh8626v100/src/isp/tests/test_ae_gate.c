#include "fh8626_ae_runtime.h"
#include <assert.h>
#include <stdint.h>
#include <limits.h>
#include <string.h>
static unsigned calls,seen_state;
static int32_t seen_error;
static int override_history;
static void notify(void *opaque,uint32_t state,int32_t error){
 struct fh8626_ae_runtime *a=opaque;
 calls++;seen_state=state;seen_error=error;
 assert(a->gate.state==state);
 if(override_history){a->history[59]=100;a->history[58]=100;}
}
int main(void){
 struct fh_ae_gate_state s={0};
 struct fh8626_ae_runtime a;
 struct fh_sensor_gc1054 sensor={0};
 uint8_t ctx[0xa80]={0};
 int32_t history[60]={0};
 /* Strict state1 dwell>limit, state2 dwell>=limit; threshold equality. */
 assert(fh_ae_gate_update(&s,0,0,0,1,2,1)==1&&s.dwell==1);
 assert(fh_ae_gate_update(&s,0,0,0,1,2,1)==0&&s.state==2&&s.dwell==0);
 assert(fh_ae_gate_update(&s,31,31,0,1,2,1)==0);
 assert(fh_ae_gate_update(&s,32,32,0,1,2,1)==1);
 s=(struct fh_ae_gate_state){0,1,99};
 assert(fh_ae_gate_update(&s,16,16,0,1,2,1)==1&&s.dwell==0);
 s=(struct fh_ae_gate_state){0,0,0};
 assert(fh_ae_gate_update(&s,0,0,0,1,2,0)==0&&s.state==2);
 s=(struct fh_ae_gate_state){0,2,0};
 assert(fh_ae_gate_update(&s,32,32,0,1,2,0)==1&&s.dwell==0);
 s=(struct fh_ae_gate_state){0,99,77};
 assert(fh_ae_gate_update(&s,0,0,0,1,2,1)==0&&s.state==0&&s.dwell==77);
 /* MULS sign bit is low32, not int64 mathematical sign. */
 s=(struct fh_ae_gate_state){0,1,0};
 assert(fh_ae_gate_update(&s,1,50000,50000,0,0,1)==0&&s.state==2);
 s=(struct fh_ae_gate_state){0,1,0};
 assert(fh_ae_gate_update(&s,1,-65536,65536,0,0,1)==1&&s.state==1);
 /* Accumulation override is AFTER sign-cross, strict >64000. */
 s=(struct fh_ae_gate_state){63999,1,0};
 assert(fh_ae_gate_update(&s,1,-1,1,0,0,1)==0&&s.accum==64000);
 assert(fh_ae_gate_update(&s,1,-1,1,0,0,1)==1&&s.accum==0&&s.dwell==0);
 s=(struct fh_ae_gate_state){INT_MAX,1,0};
 assert(fh_ae_gate_update(&s,1,0,0,0,0,1)==1&&s.accum==INT_MIN);
 s=(struct fh_ae_gate_state){0,1,0x7fffffff};
 assert(fh_ae_gate_update(&s,0,0,0,1,2,1)==1&&s.dwell==0x80000000u);
 history[0]=INT_MAX;history[59]=42;
 assert(fh_ae_history60_update(history,1)==INT_MIN);
 assert(history[58]==42&&history[59]==1);
 /* Runtime publication boundary: notify sees intermediate state1/error
  * -1000, finish then observes the new history sign-cross and returns2. */
 fh8626_ae_runtime_init_passive(&a,ctx,NULL,&sensor,NULL,NULL,NULL,NULL);
 assert(a.gate_notify==NULL);
 a.initialized=1;a.history[59]=1000;
 a.gate_notify=notify;a.gate_notify_opaque=&a;
 assert(fh8626_ae_runtime_enable_observe(&a,1)==0);
 assert(fh8626_ae_runtime_step_metric(&a,1000,2000)==0);
 assert(calls==1&&seen_state==1&&seen_error==-1000&&a.gate.state==2);
 /* A registered callback may modify shared history; finish must reread,
  * not use values captured before the callback. */
 a.gate.state=0;a.history[59]=1000;override_history=1;
 assert(fh8626_ae_runtime_step_metric(&a,1000,2000)==0);
 assert(calls==2&&seen_state==1&&a.gate.state==1);
 return 0;
}

