#include "fh8626_ae_runtime.h"
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

static uint32_t readback;
static int get_error, set_error;
static unsigned calls;
static uint32_t commanded;
static int mock_get(uint32_t *value)
{
    assert(calls == 1u); ++calls;
    if (!get_error) *value = readback;
    return get_error;
}
static int mock_stage(void *opaque, unsigned pair, uint32_t value)
{
    (void)opaque;
    assert(pair == 0u && calls == 0u); ++calls; commanded = value;
    return set_error;
}
static void put16(uint8_t *p, unsigned off, uint16_t v)
{ memcpy(p+off,&v,sizeof(v)); }
int main(void)
{
    struct fh8626_ae_runtime ae;
    struct fh_sensor_gc1054 sensor = {0};
    uint8_t cb[FH_SENSOR_CB_SIZE] = {0}, ctx[0xa80] = {0};
    void *fn = (void *)(uintptr_t)mock_get;
    memcpy(cb+0x18,&fn,sizeof(fn)); sensor.cb=cb;
    fh8626_ae_runtime_init_passive(&ae,ctx,NULL,&sensor,NULL,NULL,NULL,NULL);
    ae.stage=mock_stage;
    put16(ctx,0x38,1000); put16(ctx,0x5a,100); put16(ctx,0x5e,64);
    ctx[0x2c]=3; ctx[0x3b]=0x10;
    calls=0; readback=100;
    assert(fh8626_ae_runtime_c6e64_intt_gain(&ae,2.0f)==0);
    assert(calls==2&&commanded==200);
    assert(ae.queue[1].dirty==1&&ae.queue[1].value==128);
    assert(ctx[0x5a]==100); /* no invented early context publication */
    /* Requested=200 would yield64. Actual readback100 must yield128. */
    calls=0; readback=200;
    assert(fh8626_ae_runtime_c6e64_intt_gain(&ae,2.0f)==0);
    assert(ae.queue[1].value==64);
    /* Saturation and fine quantization; input is multiply then signed i2f. */
    calls=0; readback=100; ctx[0x3a]=0x30;
    put16(ctx,0x38,100);
    assert(fh8626_ae_runtime_c6e64_intt_gain(&ae,1e30f)==0);
    assert(commanded==100&&ae.queue[1].value==64);
    calls=0; ctx[0x3a]=0x70;
    ae.positive_delayed_gain_valid=0;
    assert(fh8626_ae_runtime_c6e64_intt_gain(&ae,1.0f)==-ENODATA);
    assert(calls==0);
    ae.positive_delayed_gain=96; ae.positive_delayed_gain_valid=1;
    put16(ctx,0x38,1000); readback=150; calls=0;
    assert(fh8626_ae_runtime_c6e64_intt_gain(&ae,1.0f)==0);
    assert(commanded==150&&ae.queue[1].value==96);
    assert(ae.positive_delayed_gain==64); /* old value queued, new retained */
    /* Error still reaches get_intt and stages gain; first diagnostic wins. */
    calls=0; set_error=-EIO; get_error=-ENOSYS; ctx[0x3a]=0;
    assert(fh8626_ae_runtime_c6e64_intt_gain(&ae,2.0f)==-EIO);
    assert(calls==2&&ae.queue[1].value==64);
    calls=0; set_error=get_error=0; readback=0;
    assert(fh8626_ae_runtime_c6e64_intt_gain(&ae,1.0f)==-ERANGE);
    calls=0; ctx[0x2c]=1;
    ae.queue[1].dirty=0;
    assert(fh8626_ae_runtime_c6e64_intt_gain(&ae,0.0f)==0);
    assert(calls==1&&commanded==1&&ae.queue[1].dirty==0);
    calls=0;
    assert(fh8626_ae_runtime_c6e64_intt_gain(&ae,NAN)==-ERANGE);
    assert(fh8626_ae_runtime_c6e64_intt_gain(&ae,INFINITY)==-ERANGE);
    assert(calls==0);
    /* Exact representable examples distinguish C7E04 from C7EB0. */
    assert(fh_ae_c7e04_factor(100,200,1)==1.25f);
    assert(fh_ae_c7e04_factor(200,100,1)==0.75f);
    assert(fh_ae_c7e04_factor(0,3,0)==1.0f+2.0f/3.0f);
    assert(fh_ae_c7e04_factor(1,1,255)==1.0f);
    assert(fh_ae_c7e04_factor(100,0,0)==0.0f);
    assert(fh_ae_c7e04_factor(100,200,1)!=fh_ae_c7eb0_factor(100,200,1));
    return 0;
}
