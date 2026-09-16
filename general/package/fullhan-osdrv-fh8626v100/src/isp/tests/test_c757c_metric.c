#include "fh8626_isp_runtime.h"
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    struct fh_isp_runtime rt;
    uint32_t mmio[FH_ISP_MMIO_SIZE / sizeof(uint32_t)] = {0};
    const uint32_t zones[9] = {100,200,300,400,900,600,700,800,500};
    fh_isp_runtime_reset(&rt);
    assert(fh_isp_runtime_attach_mmio(&rt, mmio, sizeof(mmio)) == 0);
    rt.ctx[0x3d] = 0; /* all zones */
    assert(fh_isp_runtime_apply_c757c_metric(&rt, zones) == 0);
    assert(rt.c757c_weighted_mean == 500);
    assert(rt.c757c_metric_q12 == 1431);
    assert(rt.c757c_group_value[0] == 100);
    rt.ctx[0x3d] = 1; /* center only */
    assert(fh_isp_runtime_apply_c757c_metric(&rt, zones) == 0);
    assert(rt.c757c_weighted_mean == 900);
    rt.ctx[0x3d] = 3; rt.ctx[0x3e] = 1; /* two darkest */
    assert(fh_isp_runtime_apply_c757c_metric(&rt, zones) == 0);
    assert(rt.c757c_weighted_mean == 150);
    rt.ctx[0x3d] = 4; rt.ctx[0x3e] = 1; /* two brightest */
    assert(fh_isp_runtime_apply_c757c_metric(&rt, zones) == 0);
    assert(rt.c757c_weighted_mean == 850);
    rt.ctx[0x3d] = 5;
    for (unsigned i = 0; i < 9; ++i) rt.ctx[0x4c + i] = 0;
    rt.ctx[0x4c + 7] = 1;
    assert(fh_isp_runtime_apply_c757c_metric(&rt, zones) == 0);
    assert(rt.c757c_weighted_mean == 800);
    rt.ctx[0x3d] = 6; rt.ctx[0x3e] = 8; /* brightest sorted slot */
    assert(fh_isp_runtime_apply_c757c_metric(&rt, zones) == 0);
    assert(rt.c757c_weighted_mean == 900);
    {
        uint32_t saturated[9];
        uint16_t packed = 0xa000u;
        for (unsigned i = 0; i < 9; ++i) saturated[i] = 4096u;
        rt.ctx[0x3d] = 0u;
        memcpy(rt.ctx + 0x58u, &packed, sizeof(packed));
        assert(fh_isp_runtime_apply_c757c_metric(&rt, saturated) == 0);
        assert(rt.c757c_metric_q12 == 4096u);
        memcpy(&packed, rt.ctx + 0x58u, sizeof(packed));
        assert(packed == 0xa000u); /* full metric and packed bits differ */
    }
    {
        uint8_t *cfg = calloc(1, 0x22000u);
        uint32_t *tiles;
        assert(cfg != NULL);
        assert(fh_isp_runtime_attach_isp_cfg(&rt, cfg, 0x22000u) == 0);
        tiles = (uint32_t *)(void *)(cfg + 0x5c8u);
        for (unsigned plane = 0; plane < 4; ++plane)
            for (unsigned tile = 0; tile < 256; ++tile) {
                tiles[plane * 0x400u + tile * 4u] = tile + plane;
                tiles[plane * 0x400u + tile * 4u + 1u] = 1u;
            }
        assert(fh_isp_runtime_snapshot_c6c00(&rt) == 0);
        assert(rt.ltm_group_count[0] == 36u * 4u);
        assert(rt.ltm_group_count[4] == 36u * 4u);
        assert(rt.ltm_group_count[8] == 36u * 4u);
        assert(rt.ltm_group_count[1] == 25u * 4u);
        assert(rt.ltm_group_sum[0] == 6336u);
        assert(rt.ltm_group_sum[8] == 30816u);
        {
            uint32_t raw0 = rt.ltm_group_sum[0];
            uint32_t raw8 = rt.ltm_group_sum[8];
            mmio[0x168u / sizeof(uint32_t)] = 0x1ffeu;
            assert(fh_isp_runtime_apply_c73f8(&rt) == 0);
            assert(rt.c73f8_group_value[0] == 44u);
            assert(rt.c73f8_group_value[8] == 214u);
            assert(rt.c73f8_invalid == 0u);
            assert(rt.gain_ex == 0x0fffu);
            assert(fh_isp_runtime_apply_c757c_metric(
                       &rt, rt.c73f8_group_value) == 0);
            assert(rt.ltm_group_sum[0] == raw0);
            assert(rt.ltm_group_sum[8] == raw8);

            rt.ltm_group_count[3] = 0u;
            assert(fh_isp_runtime_apply_c73f8(&rt) == 0);
            assert(rt.c73f8_group_value[3] == 0u);
            assert(rt.c73f8_invalid == 1u);
            rt.ltm_group_count[3] = 1u;
            assert(fh_isp_runtime_apply_c73f8(&rt) == 0);
            assert(rt.c73f8_invalid == 1u);
        }
        free(cfg);
    }
    {
        uint16_t v = 100u, metric = 2048u;
        memcpy(rt.ctx + 0x36u, &v, sizeof(v));
        memcpy(rt.ctx + 0x34u, &v, sizeof(v));
        memcpy(rt.ctx + 0x38u, &v, sizeof(v));
        memcpy(rt.ctx + 0x5eu, &v, sizeof(v));
        memcpy(rt.ctx + 0x5cu, &v, sizeof(v));
        memcpy(rt.ctx + 0x5au, &v, sizeof(v));
        memcpy(rt.ctx + 0x58u, &metric, sizeof(metric));
        assert(fh_isp_runtime_apply_c77dc(&rt) == 0);
        memcpy(&v, rt.ctx + 0x68u, sizeof(v));
        assert(v == 2560u); /* log2((2048^2) >> 12) in Q8 */
    }
    return 0;
}
