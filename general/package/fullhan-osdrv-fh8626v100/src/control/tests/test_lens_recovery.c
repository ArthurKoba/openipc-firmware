#include "fh8626_lens_recovery.h"
#include <assert.h>
#include <stdio.h>

static unsigned step, failures, mismatch, selected;
static uint32_t current_intt, current_gain;
static int event(void) { return failures & (1u << step++) ? -(100+(int)step) : 0; }
/* Link-time providers inject failures after touching output/state, just as a
 * driver callback can. No camera or MMIO access occurs in this executable. */
int fh_sensor_gc1054_get_intt(struct fh_sensor_gc1054 *s,uint32_t *v)
{ (void)s; *v=current_intt; return event(); }
int fh_sensor_gc1054_get_gain(struct fh_sensor_gc1054 *s,uint32_t *v)
{ (void)s; *v=current_gain; return event(); }
int fh_sensor_gc1054_set_intt(struct fh_sensor_gc1054 *s,uint32_t v)
{ (void)s; current_intt=v+(mismatch&1u); return event(); }
int fh_sensor_gc1054_set_gain(struct fh_sensor_gc1054 *s,uint32_t v)
{ (void)s; current_gain=v+((mismatch>>1)&1u); return event(); }
static int select_old(void *p,int target)
{ (void)p; assert(step==0);selected=(unsigned)target;return event(); }
static void settle(void *p) { (void)p; assert(step==1);step++; }
static void reset(unsigned f)
{ step=0; failures=f; mismatch=selected=0;current_intt=123;current_gain=96; }
int main(void)
{
    struct fh_sensor_gc1054 sensor={0};
    struct fh_lens_snapshot saved={555,777,1};
    struct fh_lens_recovery_report r;
    for(unsigned f=0;f<4;f++) {
        reset(f); saved=(struct fh_lens_snapshot){555,777,1};
        int rc=fh_lens_snapshot_read(&sensor,&saved);
        assert(step==2);
        if(f){assert(rc!=0&&!saved.valid&&saved.intt==555&&saved.gain==777);}
        else assert(!rc&&saved.valid&&saved.intt==123&&saved.gain==96);
    }
    saved=(struct fh_lens_snapshot){555,777,1};
    for(unsigned f=0;f<64;f++) {
        reset(f);
        int rc=fh_lens_restore(&sensor,1,&saved,select_old,settle,NULL,&r);
        assert(selected==1);
        if(f&1u){assert(step==1&&rc==r.select_error&&rc!=0);
            assert(current_intt==123&&current_gain==96);continue;}
        assert(step==6&&current_intt==555&&current_gain==777);
        /* Bit1 is the delay, not a failing provider. All other errors must
         * be surfaced; later setters/readbacks run after earlier failures. */
        assert(!!rc==!!(f&0x3du));
        assert(rc==(r.intt_error?r.intt_error:r.gain_error?r.gain_error:r.readback_error));
    }
    for(unsigned m=1;m<4;m++){
        reset(0);mismatch=m;
        assert(fh_lens_restore(&sensor,2,&saved,select_old,settle,NULL,&r)==-EIO);
        assert(step==6&&r.readback_error==-EIO);
    }
    reset(0);saved.valid=0;
    assert(fh_lens_restore(&sensor,1,&saved,select_old,settle,NULL,&r)==-EINVAL);
    assert(!step);
    assert(fh_lens_snapshot_read(NULL,&saved)==-EINVAL);
    puts("Lens snapshot/partial-output/64 undo failures/readback mismatch: PASS");
    return 0;
}
