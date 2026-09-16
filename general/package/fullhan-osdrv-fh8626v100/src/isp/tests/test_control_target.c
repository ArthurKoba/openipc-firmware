#include "fh8626_isp_runtime.h"
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void half(struct fh_isp_runtime *r, unsigned off, uint16_t v)
{ memcpy(r->ctx + off, &v, sizeof(v)); }

static int observe_target(void *opaque)
{
    struct fh_isp_runtime *r = opaque;
    assert(r->c757c_metric_q12 == 1280u);
    assert(r->control_target_q12 == 1600u && r->ctx[0x66] == 100u);
    return -ECANCELED;
}

int main(void)
{
    struct fh_isp_runtime r;
    fh_isp_runtime_reset(&r);
    r.ctx[0x30] = 80u;
    r.ctx[0x66] = 5u;
    assert(fh_isp_runtime_apply_control_target(&r, 256u, 0) == 0);
    assert(r.control_target_q12 == 1280u && r.ctx[0x66] == 80u);
    r.ctx[0x44] = 1u;
    half(&r, 0x38, 1000u);
    half(&r, 0x46, 100u);
    /* At zero integration the sqrt branch reaches the upper target. */
    half(&r, 0x5a, 0u);
    assert(fh_isp_runtime_apply_control_target(&r, 256u, 0) == 0);
    assert(r.control_target_q12 == 1600u);
    /* Pivot is 7%, not 70%: 1280 + floor(320*(1000-70)/1000). */
    half(&r, 0x5a, 70u);
    assert(fh_isp_runtime_apply_control_target(&r, 256u, 0) == 0);
    assert(r.control_target_q12 == 1577u);
    half(&r, 0x5a, 999u);
    assert(fh_isp_runtime_apply_control_target(&r, 256u, 0) == 0);
    assert(r.control_target_q12 == 1280u);
    /* Gain branch: powers of two make the Q8 log interpolation exact.
     * low^2/4096=64; upper^2/4096=256; observed ratio=128 => midpoint. */
    r.ctx[0x30] = 64u;
    r.ctx[0x45] = 32u;
    r.ctx[0x2f] = 0x80u;
    half(&r, 0x5a, 1000u);
    half(&r, 0x36, 64u);
    half(&r, 0x34, 64u);
    half(&r, 0x5e, 64u);
    r.c757c_metric_q12 = 1024u;
    assert(fh_isp_runtime_apply_control_target(&r, 512u, 0) == 0);
    assert(r.control_target_q12 == 768u && r.ctx[0x66] == 48u);
    assert(fh_isp_runtime_apply_control_target(&r, 512u, 1) == 0);
    assert(r.control_target_q12 == 768u);
    /* No gain denominator: stock preserves both publications. */
    assert(fh_isp_runtime_apply_control_target(&r, 0u, 1) == 0);
    assert(r.control_target_q12 == 768u && r.ctx[0x66] == 48u);
    /* Both variants restore profile target when dynamic mode is disabled. */
    r.ctx[0x44] = 0u;
    assert(fh_isp_runtime_apply_control_target(&r, 0u, 1) == 0);
    assert(r.control_target_q12 == 1024u && r.ctx[0x66] == 64u);
    {
        uint32_t mmio[0x1000] = {0};
        uint8_t *cfg = calloc(1, 0x22000u);
        uint32_t sum = 400u, count = 1u;
        assert(cfg);
        fh_isp_runtime_reset(&r);
        assert(fh_isp_runtime_attach_mmio(&r, mmio, sizeof(mmio)) == 0);
        assert(fh_isp_runtime_attach_isp_cfg(&r, cfg, 0x22000u) == 0);
        for (unsigned p = 0; p < 4; ++p)
            for (unsigned t = 0; t < 256; ++t) {
                memcpy(cfg + 0x5c8 + p * 0x1000 + t * 16, &sum, 4);
                memcpy(cfg + 0x5cc + p * 0x1000 + t * 16, &count, 4);
            }
        r.stock_runtime_started = 1u;
        r.ctx[0x10] = r.ctx[0x44] = 1u;
        r.ctx[0x30] = 80u;
        half(&r, 0x38, 1000u);
        half(&r, 0x46, 100u);
        assert(fh_isp_runtime_tick_with_control_hooks(&r, observe_target,
                    &r, NULL, NULL) == 0);
        assert(r.last_ae_error == -ECANCELED); /* diagnostic, not tick abort */
        free(cfg);
    }
    return 0;
}
