#include "fh8626_isp_runtime.h"
#include "fh8626_cf8ec_gamma_presets.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char *read_all(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    unsigned char *p;
    long n;
    assert(f != NULL);
    assert(fseek(f, 0, SEEK_END) == 0);
    n = ftell(f);
    assert(n > 0 && fseek(f, 0, SEEK_SET) == 0);
    p = malloc((size_t)n);
    assert(p != NULL && fread(p, 1, (size_t)n, f) == (size_t)n);
    fclose(f);
    *size = (size_t)n;
    return p;
}

static void check_gamma(struct fh_isp_runtime *rt, int expect_night)
{
    assert(fh_isp_runtime_apply_cfbc4(rt) == 0);
    assert(memcmp(rt->ctx + 0x5f0u, rt->ctx + 0x370u, 0x140u) == 0);
    if (expect_night)
        assert(memcmp(rt->ctx + 0x730u, fh_cf8ec_gamma_presets[5], 0x140u) == 0);
    else
        assert(memcmp(rt->ctx + 0x730u, rt->ctx + 0x4b0u, 0x140u) == 0);
}

static void check_composed_gamma(struct fh_isp_runtime *rt)
{
    uint32_t a, b, z;
    memset(rt->ctx + 0x370u, 0, 0x280u);
    rt->ctx[0x12] |= 0x40u;
    rt->ctx[0x365] = 0x18u; /* bank0=CEDC0, bank1=unchanged */
    rt->ctx[0x36c] = 0x88u; /* input8, curve8 */
    rt->ctx[0x36d] = 0x48u; /* correction8, tuning4 */
    rt->ctx[0x36e] = 0x04u; /* transform4 */
    rt->ctx[0x367] = 0x80u;
    assert(fh_isp_runtime_apply_cfbc4(rt) == 0);
    memcpy(&a, rt->ctx + 0x4b0u, 4u);
    memcpy(&b, rt->ctx + 0x4b4u, 4u);
    memcpy(&z, rt->ctx + 0x550u, 4u);
    assert(a == 0x00000000u);
    assert(b == 0x00110008u);
    assert(z == 0x00000fffu);
    assert(a == rt->mmio[0x1000u / 4u]);
    assert(a == rt->mmio[0x1180u / 4u]);
    assert(a == rt->mmio[0x1300u / 4u]);
}

static void check_profile_extent(void)
{
    struct fh_isp_runtime rt, before;
    uint8_t p[FH_ISP_PARAM_SIZE + 8];
    memset(p, 0xa5, sizeof(p));
    p[2] = 1; p[3] = 0; /* BEFB8 version high-halfword */
    for (size_t len = 0xa58; len <= sizeof(p); len++) {
        fh_isp_runtime_reset(&rt);
        memset(rt.ctx, 0x69, sizeof(rt.ctx));
        before = rt;
        assert(fh_isp_runtime_load_param(&rt, p, len) == 0);
        for (size_t i = 0; i < sizeof(rt.ctx); i++) {
            int copied = i < 4 || (i >= 0x10 && i < 0x14) ||
                         (i >= 0x2c && i < FH_ISP_PARAM_SIZE);
            uint8_t expected = copied ? (i < len ? p[i] : 0) : before.ctx[i];
            if (i == 0x367) expected |= 0x80;
            assert(rt.ctx[i] == expected);
        }
        assert(rt.profile_generation == 1 && rt.params_dirty == 1);
    }
    before = rt;
    assert(fh_isp_runtime_load_param(&rt, p, 0xa57) == -EINVAL);
    assert(memcmp(&rt, &before, sizeof(rt)) == 0);
    /* BF030 is puts, not assert/abort. Shipped GC1054 starts21120730,
     * so rejecting non-0001 high halfwords would break a valid profile. */
    p[2] = 0;
    assert(fh_isp_runtime_load_param(&rt, p, sizeof(p)) == 0);
    assert(rt.ctx[2] == 0);
    p[2] = 1; p[3] = 1;
    assert(fh_isp_runtime_load_param(&rt, p, sizeof(p)) == 0);
    assert(rt.ctx[3] == 1);
}

int main(int argc, char **argv)
{
    /* Current-Ghidra CDD6C with the real relocated banks, day profile,
     * gain64 and a zero MMIO seed. This intentionally differs from the old
     * mixed bootstrap/post-CDD6C snapshot. */
    static const uint32_t apc_day_gain64[20] = {
        0x00012010u,0x00800404u,0x007f7f7fu,0x00000000u,0x03000000u,
        0x0a140204u,0x0a141325u,0x30301325u,0x03ff0000u,0x00800402u,
        0x7e000202u,0x007f7f7eu,0x04000000u,0x09140104u,0x09140914u,
        0x70700914u,0x03ff0000u,0x00000000u,0x00000000u,0x06003f00u
    };
    struct fh_isp_runtime rt;
    uint32_t mmio[0x1000] = {0}, night_apc[20], wlight_apc[20];
    unsigned char *sreg;
    size_t size;
    uint16_t limit;
    assert(argc == 2);
    check_profile_extent();
    sreg = read_all(argv[1], &size);
    fh_isp_runtime_reset(&rt);
    assert(rt.d1db0_coeffs_valid == 1);
    assert(rt.d1db0_coeffs.c16 == -173 && rt.d1db0_coeffs.c40 == -83);
    assert(rt.d16a4_rows != NULL);
    assert(fh_isp_runtime_attach_mmio(&rt, mmio, sizeof(mmio)) == 0);
    rt.ctx[0xa70]=1; /* model successful C531C initialization before NR3D */
    { uint32_t mode=1;memcpy(rt.ctx+0x11ac,&mode,4); } /* driver query fixture */
    assert(fh_isp_runtime_load_sreg_profile(&rt, sreg, size, "day") == 0);
    for (unsigned i=0xa58; i<0xa5c; i++) assert(rt.ctx[i] == 0);
    assert(rt.profile_generation == 1u);
    assert(rt.nr3d_warmup_left == ((rt.ctx[0x3a] & 7u) + 1u));
    assert(fh_isp_runtime_nr3d_ready(&rt) == 0);
    fh_isp_runtime_accept_stats_epoch(&rt, 1u);
    assert(fh_isp_runtime_publish_control_metric(&rt, 1u, 1278, 1280) == 0);
    assert(rt.control_delta == -2 && rt.control_delta_aggregate == -2);
    assert(fh_isp_runtime_publish_control_metric(&rt, 1u, 1, 2) == 0);
    assert(rt.control_delta == -2); /* same epoch is accepted only once */
    assert(fh_isp_runtime_apply_c73f8(&rt) == 0);
    assert(rt.gain_epoch == rt.control_epoch && rt.nr3d_warmup_left == 0u);
    assert(fh_isp_runtime_nr3d_ready(&rt) == 1);
    fh_isp_runtime_accept_stats_epoch(&rt, 2u);
    assert(fh_isp_runtime_publish_control_metric(&rt, 1u, 0, 0) == -ESTALE);
    assert(fh_isp_runtime_publish_control_metric(&rt, 2u, 1281, 1280) == 0);
    assert(rt.control_delta == 1 && rt.control_delta_aggregate == 1);
    assert(fh_isp_runtime_apply_c73f8(&rt) == 0);
    assert(fh_isp_runtime_nr3d_ready(&rt) == 1);
    memcpy(&limit, rt.ctx + 0x38, sizeof(limit));
    assert(limit == 899u && rt.ctx[0x3d] == 0u);
    check_gamma(&rt, 0);
    check_composed_gamma(&rt);
    {
        uint32_t valid = 1u, total_gain = 64u << 12;
        memcpy(rt.ctx + 0xa70, &valid, 4u);
        memcpy(rt.ctx + 0x60, &total_gain, 4u);
    }
    assert(fh_isp_runtime_apply_cdd6c(&rt) == 0);
    for (unsigned i = 0; i < 20u; ++i) {
        if (mmio[0x528u / 4u + i] != apc_day_gain64[i])
            fprintf(stderr, "APC day[%u] got=%08x expected=%08x\n", i,
                    mmio[0x528u / 4u + i], apc_day_gain64[i]);
        assert(mmio[0x528u / 4u + i] == apc_day_gain64[i]);
    }
    rt.exposure_history[0] = 0x1234u;
    rt.gain_history[0] = 0x5678u;
    assert(fh_isp_runtime_load_sreg_profile(&rt, sreg, size, "night") == 0);
    assert(rt.profile_generation == 2u && rt.nr3d_warmup_left == 0u);
    assert(rt.exposure_history[0] == 0x1234u && rt.gain_history[0] == 0x5678u);
    assert(fh_isp_runtime_nr3d_ready(&rt) == 1);
    memcpy(&limit, rt.ctx + 0x38, sizeof(limit));
    assert(limit == 745u && rt.ctx[0x3d] == 1u);
    check_gamma(&rt, 1);
    {
        uint32_t valid = 1u, total_gain = 64u << 12;
        memcpy(rt.ctx + 0xa70, &valid, 4u);
        memcpy(rt.ctx + 0x60, &total_gain, 4u);
    }
    assert(fh_isp_runtime_apply_cdd6c(&rt) == 0);
    memcpy(night_apc, mmio + 0x528u / 4u, sizeof(night_apc));
    assert(fh_isp_runtime_load_sreg_profile(&rt, sreg, size, "wlight") == 0);
    assert(rt.profile_generation == 3u && rt.nr3d_warmup_left == 0u);
    fh_isp_runtime_set_nr3d_enabled(&rt, 0);
    assert(fh_isp_runtime_nr3d_ready(&rt) == 0 && !(rt.ctx[0x11] & 0x40u));
    fh_isp_runtime_set_nr3d_enabled(&rt, 1);
    memcpy(&limit, rt.ctx + 0x38, sizeof(limit));
    assert(limit == 745u && rt.ctx[0x3d] == 0u);
    check_gamma(&rt, 1);
    {
        uint32_t valid = 1u, total_gain = 64u << 12;
        memcpy(rt.ctx + 0xa70, &valid, 4u);
        memcpy(rt.ctx + 0x60, &total_gain, 4u);
    }
    assert(fh_isp_runtime_apply_cdd6c(&rt) == 0);
    memcpy(wlight_apc, mmio + 0x528u / 4u, sizeof(wlight_apc));
    assert(memcmp(night_apc, apc_day_gain64, sizeof(night_apc)) != 0);
    assert(memcmp(wlight_apc, apc_day_gain64, sizeof(wlight_apc)) != 0);
    assert(fh_isp_runtime_load_sreg_profile(&rt, sreg, size, "missing") == -ENOENT);
    free(sreg);
    return 0;
}
