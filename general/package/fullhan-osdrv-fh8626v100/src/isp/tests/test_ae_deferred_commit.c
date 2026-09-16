#include "fh8626_ae_runtime.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>

struct commit_log {
    unsigned pair[5];
    uint32_t value[5];
    unsigned count;
    unsigned flushes;
    int fail_pair4;
};

static int mock_get_gain(uint32_t *value) { *value = 200u; return 0; }
static int mock_get_intt(uint32_t *value) { *value = 200u; return 0; }

static int log_stage(void *opaque, unsigned pair, uint32_t value)
{
    struct commit_log *log = opaque;
    assert(log->count < 5u);
    log->pair[log->count] = pair;
    log->value[log->count++] = value;
    return pair == 4u ? log->fail_pair4 : 0;
}

static int log_flush(void *opaque)
{
    ((struct commit_log *)opaque)->flushes++;
    return 0;
}

int main(void)
{
    struct fh8626_ae_runtime ae;
    struct fh_sensor_gc1054 sensor = {0};
    struct commit_log log = {0};
    uint8_t callbacks[FH_SENSOR_CB_SIZE] = {0};
    uint8_t ctx[0xa80] = {0};
    void *fn;
    uint32_t published_timing = 0u;

    fn = (void *)(uintptr_t)mock_get_gain;
    memcpy(callbacks + 0x0cu, &fn, sizeof(fn));
    fn = (void *)(uintptr_t)mock_get_intt;
    memcpy(callbacks + 0x18u, &fn, sizeof(fn));
    sensor.cb = callbacks;

    ctx[0x2fu] = 0x80u; /* shipped negative-selector family */
    ctx[0x1au] = 100u;
    ctx[0x34u] = 200u;
    ctx[0x36u] = 100u;
    ctx[0x38u] = 95u;
    ctx[0x5au] = 200u; /* C73F8 epoch-aligned exposure/gain input */
    ctx[0x5eu] = 200u;
    ctx[0x3fu] = 0u;
    ctx[0x31u] = ctx[0x32u] = 10u; ctx[0x33u] = 5u;

    fh8626_ae_runtime_init(&ae, ctx, NULL, &sensor, NULL, NULL, NULL, NULL,
                           log_stage, log_flush, &log);
    ae.published_timing_slot = &published_timing;
    ae.gate.state = 2u; /* closed gate for deferred-bounds-only regression */
    assert(fh8626_ae_runtime_enable_observe(&ae, 1) == 0);
    assert(fh8626_ae_runtime_enable_commit(&ae, 1) == 0);

    /* Equal nonzero metric/target keeps C883C closed. C90D4 still stages its
     * bounds and C6AC8 must commit them once, in pair order, at frame end. */
    assert(fh8626_ae_runtime_step_metric(&ae, 1280u, 1280u) == 0);
    assert(log.count == 3u);
    assert(log.pair[0] == 0u && log.value[0] == 95u);
    assert(log.pair[1] == 1u && log.value[1] == 100u);
    assert(log.pair[2] == 4u && log.value[2] == 1u);
    assert(log.flushes == 1u);
    for (unsigned i = 0u; i < 5u; ++i) assert(ae.queue[i].dirty == 0u);
    assert(ae.queue[0].value == 95u);
    assert(ae.queue[1].value == 100u);
    assert(ae.queue[4].value == 1u);
    assert(published_timing == 1u);
    /* Profile changes select the separately restored positive family without
     * resetting persistent history or the negative-family Q8 state. */
    ae.history[17] = 123;
    ae.q8_aux = 320u;
    ctx[0x2f] = 0;
    assert(fh8626_ae_runtime_enable_commit(&ae, 1) == 0);
    assert(ae.commit_enabled == 1);
    assert(ae.history[17] == 123 && ae.q8_aux == 320u);
    ctx[0x2f] = 0x80u;
    assert(fh8626_ae_runtime_enable_commit(&ae, 1) == 0);
    assert(ae.history[17] == 123 && ae.q8_aux == 320u);
    assert(fh8626_ae_runtime_enable_commit(&ae, 0) == 0);
    log.count = 0u;
    assert(fh8626_ae_runtime_step_metric(&ae, 4096u, 1280u) == 0);
    assert(ae.measured == 4096u && ae.error == 2816);
    ae.queue[4].dirty = 1u;
    ae.queue[4].value = 3u;
    log.fail_pair4 = -EIO;
    assert(fh8626_ae_runtime_step_metric(&ae, 1280u, 1280u) == -EIO);
    assert(published_timing == 3u && ae.queue[4].dirty == 0u);
    log.fail_pair4 = 0;
    /* Exercise real queued ISP writers, not the logging override. */
    {
        uint32_t mmio[0x180u / 4u] = {0};
        uint32_t active = 1u;
        ae.stage = NULL;
        ae.flush = NULL;
        ae.isp_mmio = mmio;
        memcpy(ctx + 0xa70u, &active, sizeof(active));
        ae.queue[2].dirty = 1u;
        ae.queue[2].value = 64u;
        assert(fh8626_ae_runtime_step_metric(&ae, 1280u, 1280u) == 0);
        assert(mmio[0x168u/4u] == 0x02000200u);
        assert(mmio[0x16cu/4u] == 0x02000200u);
        ae.queue[2].dirty = ae.queue[3].dirty = 1u;
        ae.queue[2].value = 128u;
        ae.queue[3].value = 256u;
        assert(fh8626_ae_runtime_step_metric(&ae, 1280u, 1280u) == 0);
        assert(mmio[0x168u/4u] == 0x02000200u); /* pair3 runs last */
        active = 0u;
        memcpy(ctx + 0xa70u, &active, sizeof(active));
        ae.queue[2].dirty = 1u;
        ae.queue[2].value = 1u;
        assert(fh8626_ae_runtime_step_metric(&ae, 1280u, 1280u) == -EAGAIN);
        assert(mmio[0x168u/4u] == 0x02000200u);
        assert(ae.queue[2].dirty == 0u);
        assert(ae.queue[2].value == 1u); /* failure still preserves payload */
        active = 1u;
        memcpy(ctx + 0xa70u, &active, sizeof(active));
        ae.queue[2].dirty = 2u; /* noncanonical dirty must not execute */
        ae.queue[2].value = 7u;
        assert(fh8626_ae_runtime_step_metric(&ae, 1280u, 1280u) == 0);
        assert(mmio[0x168u/4u] == 0x02000200u);
        assert(ae.queue[2].dirty == 0u && ae.queue[2].value == 7u);
    }
    return 0;
}
