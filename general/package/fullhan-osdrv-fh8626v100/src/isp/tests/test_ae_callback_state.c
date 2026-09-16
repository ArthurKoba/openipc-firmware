#include "fh8626_isp_runtime.h"
#include "fh8626_control_status_tail.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>

static uint32_t mock_intt = 0x1234u;
static uint32_t mock_gain = 0x56u;
static int fail_intt;
static int fail_gain;
struct tail_probe {struct fh_isp_runtime *rt;int mask_rc,timing_rc;unsigned phase;};
static int mask_query(void *opaque,uint32_t *value)
{
    struct tail_probe *p=opaque;assert(p->phase++==0);
    *value=6u;return p->mask_rc; /* mutate output even on failure */
}
static int timing_query(void *opaque,uint32_t *out)
{
    struct tail_probe *p=opaque;assert(p->phase++==1);
    assert(p->rt->ae_state.slot[5]==500&&p->rt->ae_state.slot[6]==600);
    out[0]=88;out[1]=99;out[2]=100;out[3]=101;return p->timing_rc;
}
static unsigned log_count,log_slot[12];
static uint32_t log_value[12];
static int log_invalid[12];
static void log_event(void *opaque,unsigned slot,uint32_t value,int invalid)
{
    struct fh_isp_runtime *rt=opaque;
    assert(log_count<12u);
    /* Mirroring must already have completed before any log notification. */
    for(unsigned i=0;i<7;i++)assert(rt->ae_stats[i].value==rt->ae_state.slot[i]);
    log_slot[log_count]=slot;log_value[log_count]=value;log_invalid[log_count++]=invalid;
}

static int get_intt(uint32_t *value)
{
    if (fail_intt) return -EIO;
    *value = mock_intt;
    return 0;
}

static int get_gain(uint32_t *value)
{
    if (fail_gain) return -EIO;
    *value = mock_gain;
    return 0;
}

static void put_callback(uint8_t table[FH_SENSOR_CB_SIZE], unsigned off, void *fn)
{
    assert(off + sizeof(fn) <= FH_SENSOR_CB_SIZE);
    memcpy(table + off, &fn, sizeof(fn));
}

int main(void)
{
    struct fh_isp_runtime rt;
    struct fh_sensor_gc1054 sensor;
    uint32_t mmio[0x200] = {0};
    uint8_t callbacks[FH_SENSOR_CB_SIZE] = {0};

    fh_isp_runtime_reset(&rt);
    memset(&sensor, 0, sizeof(sensor));
    put_callback(callbacks, 0x0cu, (void *)(uintptr_t)get_gain);
    put_callback(callbacks, 0x18u, (void *)(uintptr_t)get_intt);
    sensor.cb = callbacks;
    rt.mmio = mmio;
    rt.mmio_size = sizeof(mmio);
    rt.sensor = &sensor;
    mmio[0x168u >> 2] = 0x1abcu;

    assert(fh_isp_runtime_cb890_init_ae_state(&rt) == 0);
    assert(rt.ae_state.slot[1] == mock_intt);
    assert(rt.ae_state.slot[2] == mock_gain);
    assert(rt.ae_state.slot[3] == (0x1abcu & 0x1fffu));

    rt.ctx[0x11] = 1u; /* adjacent byte must not enable C949C */
    mock_intt = 1u;
    assert(fh_isp_runtime_c949c_update_known(&rt) == 0);
    assert(rt.ae_state.slot[1] != 1u);
    rt.ctx[0x10] = 1u;
    rt.ctx[0x11] = 0u;
    mock_intt = 0x2345u;
    mock_gain = 0x67u;
    mmio[0x168u >> 2] = 0x1550u;
    assert(fh_isp_runtime_c949c_update_known(&rt) == 0);
    assert(rt.ae_state.slot[1] == 0x1234u); /* pre-AE must not publish */
    rt.ctx[0xa70] = 1u;
    assert(fh_isp_runtime_c949c_publish_tail(&rt, 6u, 1u) == 0);
    assert(rt.ae_state.slot[1] == mock_intt);
    assert(rt.ae_state.slot[2] == mock_gain);
    assert(rt.ae_state.slot[3] == ((0x1550u >> 3) & 0x3ffu));
    {
        uint16_t v16;
        uint32_t packed;
        memcpy(&v16, rt.ctx + 0x5au, 2u); assert(v16 == mock_intt);
        memcpy(&v16, rt.ctx + 0x5cu, 2u); assert(v16 == ((0x1550u >> 3) & 0x3ffu));
        memcpy(&v16, rt.ctx + 0x5eu, 2u); assert(v16 == mock_gain);
        memcpy(&v16, rt.ctx + 0x64u, 2u); assert(v16 == mock_gain);
        memcpy(&packed, rt.ctx + 0x60u, 4u);
        assert((packed & 0xfffu) == 500u);
        assert((packed >> 12) == ((((0x1550u >> 3) & 0x3ffu) * mock_gain) >> 6));
    }

    fail_intt = 1;
    fail_gain = 1;
    mock_intt = 1u;
    mock_gain = 2u;
    assert(fh_isp_runtime_c949c_update_known(&rt) == 0);
    assert(rt.ae_state.slot[1] == 0x2345u);
    assert(rt.ae_state.slot[2] == 0x67u);
    assert(fh_isp_runtime_c949c_publish_tail(&rt, 6u, 2u) == 0);
    assert(rt.ae_state.slot[1] == 0x2345u && rt.ae_state.slot[2] == 0x67u);
    fail_intt = fail_gain = 0;
    rt.ctx[0x10] = 0u; /* publication is not gated by AE enable */
    mock_intt = 0x12345678u;
    mock_gain = 0xabcdefu;
    rt.c757c_metric_q12 = 4096u;
    rt.control_target_q12 = 1280u;
    assert(fh_isp_runtime_c949c_publish_tail(&rt, 2u, 2u) == 0);
    assert(rt.ae_state.slot[1] == mock_intt); /* preserve all 32 callback bits */
    assert(rt.ae_state.slot[2] == 0x67u); /* gain mask absent */
    assert(rt.ae_state.slot[5] == 4096u && rt.ae_state.slot[6] == 1280u);
    assert(rt.ae_state.slot[7] == 2u && rt.ae_state.dirty_mask == 2u);
    rt.ae_stats[2].reserved[0] = 0xfeedu;
    assert(fh_isp_runtime_c9898_ingest_stat0(&rt, 99u) == 0);
    for (unsigned i = 0u; i < 7u; ++i)
        assert(rt.ae_stats[i].value == rt.ae_state.slot[i]);
    assert(rt.ae_stats[2].reserved[0] == 0xfeedu);
    rt.ae_log=log_event;rt.ae_log_opaque=&rt;
    rt.ae_state.dirty_mask=0x1ffu;rt.ae_state.slot[8]=9u;
    assert(fh_isp_runtime_c9898_ingest_stat0(&rt,100u)==0&&log_count==9u);
    for(unsigned i=0;i<9;i++)assert(log_slot[i]==i&&!log_invalid[i]);
    assert(log_value[0]==100u&&log_value[7]==2u&&log_value[8]==9u);
    log_count=0;rt.ae_state.slot[7]=3u;rt.ae_state.slot[8]=10u;
    assert(fh_isp_runtime_c9898_ingest_stat0(&rt,101u)==-ERANGE&&log_count==8u);
    assert(log_slot[7]==7u&&log_invalid[7]&&rt.last_ae_log_error==-ERANGE);
    log_count=0;rt.ae_state.slot[7]=2u;rt.ae_state.dirty_mask=0;
    assert(fh_isp_runtime_c9898_ingest_stat0(&rt,102u)==-ERANGE&&log_count==1u);
    assert(log_slot[0]==8u&&log_invalid[0]&&log_value[0]==10u);
    log_count=0;rt.ae_state.slot[8]=0;
    assert(fh_isp_runtime_c9898_ingest_stat0(&rt,103u)==0&&log_count==0);
    assert(rt.last_ae_log_error==0&&rt.ae_stats[2].reserved[0]==0xfeedu);
    rt.ae_log=NULL;
    for(unsigned failures=0;failures<4;failures++){
        struct tail_probe p={&rt,(failures&1)?-EIO:0,(failures&2)?-ETIMEDOUT:0,0};
        rt.ae_state.dirty_mask=2;rt.ae_state.slot[0]=22;rt.ae_state.slot[2]=33;
        rt.c757c_metric_q12=500;rt.control_target_q12=600;mock_gain=123;
        int expected=p.mask_rc?p.mask_rc:p.timing_rc;
        assert(fh_control_status_tail(&rt,2,mask_query,timing_query,&p)==expected);
        assert(p.phase==2&&rt.last_control_tail_error==expected);
        assert(rt.ae_state.dirty_mask==(p.mask_rc?2u:6u));
        assert(rt.ae_state.slot[0]==(p.timing_rc?22u:88u));
        assert(rt.ae_state.slot[1]==mock_intt&&rt.ae_state.slot[2]==(p.mask_rc?33u:123u));
        for(unsigned i=0;i<7;i++)assert(rt.ae_stats[i].value==rt.ae_state.slot[i]);
        assert(rt.ae_state.slot[7]==2&&rt.ae_stats[2].reserved[0]==0xfeedu);
    }
    assert(fh_control_status_tail(&rt,2,NULL,NULL,NULL)==-ENOSYS);
    assert(fh_control_status_tail(NULL,2,NULL,NULL,NULL)==-EINVAL);
    return 0;
}
