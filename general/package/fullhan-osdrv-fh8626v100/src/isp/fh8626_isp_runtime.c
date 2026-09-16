#include "fh8626_isp_runtime.h"
#include "fh8626_stock_sin_q7_lut.h"
#include "fh8626_stock_d1724_ref.h"
#include "fh8626_d0b2c_curve_banks.h"
#include "fh8626_cdd6c_preset_banks.h"
#include "fh8626_cf8ec_gamma_presets.h"
#include "fh8626_cedc0_tables.h"
#include "fh8626_stock_ae_state_ref.h"

_Static_assert(FH_ISP_CTX_SIZE > FH_ISP_SENSOR_CB_OFF + FH_ISP_SENSOR_CB_SIZE, "ISP context too small");
_Static_assert(FH_ISP_MMIO_PTR_OFF == FH_ISP_PARAM_SIZE, "stock MMIO pointer must follow SREG payload");

#include <errno.h>
#include <string.h>

static inline uint32_t rd(const struct fh_isp_runtime *rt, unsigned off)
{
    return rt->mmio[off >> 2];
}

static inline void wr(struct fh_isp_runtime *rt, unsigned off, uint32_t v)
{
    rt->mmio[off >> 2] = v;
}


struct fh_reg_init_pair { uint16_t off; uint32_t val; };

/* All236 straight-line writes before C48CC, in ARM execution order.
 * Derived from current-Ghidra C4998 prefix execution, 2026-09-05.
 * Transient270..30C defaults are retained even though C48CC rewrites them.
 * Initial80-word gamma pointer after C48CC remains a separate open binding. */
static const struct fh_reg_init_pair c4998_static_defaults[] = {
    {0x0008, 0xffffffffu},
    {0x0034, 0x40302010u},
    {0x0038, 0x80706050u},
    {0x003c, 0xc0b0a090u},
    {0x0040, 0x10101010u},
    {0x0044, 0x10101010u},
    {0x0048, 0x10101010u},
    {0x004c, 0x40302010u},
    {0x0050, 0x80706050u},
    {0x0054, 0xc0b0a090u},
    {0x0058, 0x0000e0d0u},
    {0x005c, 0x00000000u},
    {0x0060, 0x00000000u},
    {0x00dc, 0x00040000u},
    {0x00e0, 0x000c0008u},
    {0x00e4, 0x00140010u},
    {0x00e8, 0x001c0018u},
    {0x00ec, 0x00240020u},
    {0x00f0, 0x002c0028u},
    {0x00f4, 0x00400000u},
    {0x00f8, 0x00c00080u},
    {0x00fc, 0x01400100u},
    {0x0100, 0x01c00180u},
    {0x0104, 0x02400200u},
    {0x0108, 0x02c00280u},
    {0x010c, 0x00100010u},
    {0x0110, 0x00100010u},
    {0x0114, 0x00100010u},
    {0x0118, 0x00100010u},
    {0x011c, 0x00100010u},
    {0x0120, 0x00100010u},
    {0x0124, 0x00000000u},
    {0x017c, 0x003a0000u},
    {0x0180, 0x00af0074u},
    {0x0184, 0x00300000u},
    {0x0188, 0x008f0060u},
    {0x018c, 0x00100fffu},
    {0x0190, 0x00000400u},
    {0x0194, 0x3ffffbffu},
    {0x0198, 0x3ff00001u},
    {0x019c, 0x3feffc00u},
    {0x01a0, 0x000ffbfeu},
    {0x01a4, 0x003a0000u},
    {0x01a8, 0x00af0074u},
    {0x01ac, 0x00300000u},
    {0x01b0, 0x008f0060u},
    {0x01b4, 0x00100fffu},
    {0x01b8, 0x00000400u},
    {0x01bc, 0x3ffffbffu},
    {0x01c0, 0x3ff00001u},
    {0x01c4, 0x3feffc00u},
    {0x01c8, 0x000ffbfeu},
    {0x0224, 0x02000200u},
    {0x0228, 0x02000200u},
    {0x022c, 0x00000000u},
    {0x0230, 0x00000000u},
    {0x043c, 0x02000200u},
    {0x0440, 0x02000200u},
    {0x0444, 0x00000000u},
    {0x0448, 0x00000000u},
    {0x0234, 0x00442404u},
    {0x0238, 0x3306e0ecu},
    {0x023c, 0x00034620u},
    {0x0240, 0x0000270du},
    {0x0244, 0x0000bc6fu},
    {0x0248, 0x0da70bf6u},
    {0x024c, 0x004ad5ffu},
    {0x0250, 0x000010c9u},
    {0x0254, 0x80800000u},
    {0x0258, 0x60040102u},
    {0x0270, 0x017b0000u},
    {0x0274, 0x040b02d3u},
    {0x0278, 0x06270526u},
    {0x027c, 0x07e40711u},
    {0x0280, 0x095308a4u},
    {0x0284, 0x0a8009f1u},
    {0x0288, 0x0b790b03u},
    {0x028c, 0x0c460be4u},
    {0x0290, 0x0cee0c9eu},
    {0x0294, 0x0d790d37u},
    {0x0298, 0x0deb0db5u},
    {0x029c, 0x0e490e1cu},
    {0x02a0, 0x0e970e72u},
    {0x02a4, 0x0ed60eb8u},
    {0x02a8, 0x0f0b0ef2u},
    {0x02ac, 0x0f360f22u},
    {0x02b0, 0x0f840f49u},
    {0x02b4, 0x0fc70facu},
    {0x02b8, 0x0fe60fd9u},
    {0x02bc, 0x0ff40feeu},
    {0x02c0, 0x00000ff8u},
    {0x02c4, 0x00400000u},
    {0x02c8, 0x009a006fu},
    {0x02cc, 0x00e800c2u},
    {0x02d0, 0x0130010cu},
    {0x02d4, 0x01730152u},
    {0x02d8, 0x01b40194u},
    {0x02dc, 0x01f201d3u},
    {0x02e0, 0x022f0211u},
    {0x02e4, 0x0286024cu},
    {0x02e8, 0x02f702bfu},
    {0x02ec, 0x0363032du},
    {0x02f0, 0x03cc0398u},
    {0x02f4, 0x04c80400u},
    {0x02f8, 0x06420588u},
    {0x02fc, 0x07a706f7u},
    {0x0300, 0x08fc0853u},
    {0x0304, 0x0a4509a2u},
    {0x0308, 0x0b840ae6u},
    {0x030c, 0x00000c1eu},
    {0x0384, 0x700001ffu},
    {0x0388, 0xa92a484bu},
    {0x038c, 0x00160000u},
    {0x0390, 0x0042002cu},
    {0x0394, 0x006e0058u},
    {0x0398, 0x009a0084u},
    {0x039c, 0x000000b0u},
    {0x03a0, 0x00480024u},
    {0x03a4, 0x0090006cu},
    {0x03a8, 0x03000007u},
    {0x03ac, 0x01000000u},
    {0x03b0, 0x01000100u},
    {0x03b4, 0x00045df0u},
    {0x03b8, 0x00000000u},
    {0x03bc, 0x01000200u},
    {0x03c0, 0x01000200u},
    {0x03c4, 0x01000200u},
    {0x03c8, 0x014c016eu},
    {0x03cc, 0x00000200u},
    {0x03d0, 0x1e310000u},
    {0x03d4, 0x02000000u},
    {0x03d8, 0x1f200000u},
    {0x03dc, 0x00000000u},
    {0x03e0, 0x1e5f0200u},
    {0x03e4, 0x00000000u},
    {0x03e8, 0x02000000u},
    {0x03ec, 0x00000200u},
    {0x03f0, 0x1e310000u},
    {0x03f4, 0x02000000u},
    {0x03f8, 0x1f200000u},
    {0x03fc, 0x00000000u},
    {0x0400, 0x1e5f0200u},
    {0x0404, 0x00000000u},
    {0x0408, 0x02000000u},
    {0x040c, 0x71767b7fu},
    {0x0410, 0x5d62676cu},
    {0x0414, 0x494e5358u},
    {0x0418, 0x353a3f44u},
    {0x041c, 0x21262b30u},
    {0x0420, 0x0d12171cu},
    {0x0424, 0x007a0006u},
    {0x0428, 0x016c011au},
    {0x042c, 0x0064000au},
    {0x0430, 0x00101010u},
    {0x0434, 0x00000000u},
    {0x0438, 0x02000000u},
    {0x044c, 0x0000e788u},
    {0x0450, 0x80020d54u},
    {0x0454, 0x2000640au},
    {0x0458, 0x2000640au},
    {0x045c, 0x2000640au},
    {0x0460, 0x2000640au},
    {0x0464, 0x1fff0accu},
    {0x0468, 0x15662002u},
    {0x046c, 0x00040000u},
    {0x0470, 0x78553c26u},
    {0x0474, 0x02fa0045u},
    {0x0478, 0x0fe10cf6u},
    {0x047c, 0x4a3e3219u},
    {0x0480, 0x786c6256u},
    {0x0484, 0x00000000u},
    {0x0488, 0x000000b4u},
    {0x0490, 0x00000960u},
    {0x0498, 0x10b80a10u},
    {0x049c, 0x00000010u},
    {0x04a0, 0x00000010u},
    {0x04a4, 0x000f0008u},
    {0x04a8, 0x003800fcu},
    {0x04ac, 0x0f8040eeu},
    {0x04d8, 0x02590132u},
    {0x04dc, 0x0f530074u},
    {0x04e0, 0x02000eadu},
    {0x04e4, 0x0e530200u},
    {0x04e8, 0x00000fadu},
    {0x04ec, 0x04000400u},
    {0x04f0, 0x00000031u},
    {0x04f8, 0x00140000u},
    {0x04fc, 0x00100010u},
    {0x0514, 0x00262940u},
    {0x0518, 0x0a0400c8u},
    {0x051c, 0x5007703cu},
    {0x0520, 0x00180026u},
    {0x0524, 0x0000d103u},
    {0x0528, 0x00000404u},
    {0x052c, 0x00800404u},
    {0x0530, 0x007f7f7fu},
    {0x0534, 0x00000000u},
    {0x0538, 0x03000000u},
    {0x053c, 0x0a140204u},
    {0x0540, 0x0a141325u},
    {0x0544, 0x70701325u},
    {0x0548, 0x03ff0004u},
    {0x054c, 0x01000404u},
    {0x0550, 0x7f7f7e7eu},
    {0x0554, 0x0000007fu},
    {0x0558, 0x04000000u},
    {0x055c, 0x0a140204u},
    {0x0560, 0x0a141325u},
    {0x0564, 0xc0c01325u},
    {0x0568, 0x03ff0000u},
    {0x056c, 0x00040010u},
    {0x0570, 0x000f0555u},
    {0x0574, 0x06000000u},
    {0x0578, 0x0001040fu},
    {0x057c, 0xffffffffu},
    {0x0580, 0xffffffffu},
    {0x0584, 0xffffffffu},
    {0x0588, 0xffffffffu},
    {0x058c, 0xffffffffu},
    {0x0590, 0xffffffffu},
    {0x0594, 0xffffffffu},
    {0x0598, 0xffffffffu},
    {0x059c, 0x00000005u},
    {0x05a0, 0x0000bf90u},
    {0x05a4, 0x02d11494u},
    {0x05a8, 0x01606c22u},
    {0x05ac, 0x080e62aau},
    {0x05b0, 0x0007fc08u},
    {0x05b4, 0x00032014u},
    {0x05c8, 0x00000040u},
    {0x05cc, 0x00800000u},
    {0x05d0, 0x03ff0100u},
    {0x01f0, 0x00009015u},
    {0x0084, 0x00000000u},
    {0x0088, 0x00000000u},
    {0x0068, 0x00000004u},
};

int fh_isp_runtime_apply_c4998_static_defaults(struct fh_isp_runtime *rt)
{
    size_t i;
    if (!rt || !rt->mmio) return -EINVAL;
    for (i = 0; i < sizeof(c4998_static_defaults)/sizeof(c4998_static_defaults[0]); ++i)
        wr(rt, c4998_static_defaults[i].off, c4998_static_defaults[i].val);
    return 0;
}

/* C50A8/C50AC/C50BC: after C48CC and the initial80-word LUT copy.
 * Do not execute this tail as part of the straight-line prefix. */
static void c4998_finish_defaults(struct fh_isp_runtime *rt)
{
    wr(rt, 0x178, 0x00000044u);
    wr(rt, 0x024, 0x0a71eb14u);
    wr(rt, 0x028, (rd(rt, 0x028) & 0x00002000u) | 0x00000610u);
}

static const uint32_t stock_d16a4_rows[8][6] = {
    {0x80808080u,0x80808080u,0x80808080u,0x80808080u,0x40506070u,0x00102030u},
    {0x787b7e7fu,0x60676e73u,0x4e53575cu,0x383e4349u,0x1e252b32u,0x00080f17u},
    {0x80808080u,0x5969767eu,0x38404850u,0x1b212830u,0x090d1116u,0x00020406u},
    {0x80808080u,0x69777e7fu,0x333f4d5eu,0x181d232au,0x0a0d1014u,0x00030507u},
    {0x80808080u,0x777b7e7fu,0x676c7074u,0x50565c62u,0x2f384149u,0x000e1a25u},
    {0x80808080u,0x80808080u,0x80808080u,0x6872797eu,0x13284259u,0x0002040au},
    {0x80808080u,0x80808080u,0x80808080u,0x80808080u,0x6271797eu,0x0006123fu},
    {0x80808080u,0x80808080u,0x80808080u,0x80808080u,0x797e8080u,0x00062462u},
};

void fh_isp_runtime_reset(struct fh_isp_runtime *rt)
{
    if (rt) {
        memset(rt, 0, sizeof(*rt));
        rt->d1db0_coeffs.c16 = -173;
        rt->d1db0_coeffs.c20 = -339;
        rt->d1db0_coeffs.c24 = 512;
        rt->d1db0_coeffs.c32 = 512;
        rt->d1db0_coeffs.c36 = -429;
        rt->d1db0_coeffs.c40 = -83;
        rt->d1db0_coeffs_valid = 1;
        rt->d16a4_rows = stock_d16a4_rows;
        rt->total_gain_low12 = 500u;
        rt->nr3d_enabled = 1;
    }
}

static uint16_t ctx_u16(const struct fh_isp_runtime *rt, unsigned off)
{
    uint16_t v;
    memcpy(&v, rt->ctx + off, sizeof(v));
    return v;
}

static uint32_t ctx_u32(const struct fh_isp_runtime *rt, unsigned off)
{
    uint32_t v;
    memcpy(&v, rt->ctx + off, sizeof(v));
    return v;
}

static void ctx_set_u16(struct fh_isp_runtime *rt, unsigned off, uint16_t v)
{
    memcpy(rt->ctx + off, &v, sizeof(v));
}

static void ctx_set_u32(struct fh_isp_runtime *rt, unsigned off, uint32_t v)
{
    memcpy(rt->ctx + off, &v, sizeof(v));
}


/* D2748: floor(log2(v)), with stock's zero result for v == 0. */
static unsigned stock_log2_u32(uint32_t v)
{
    unsigned n = 0;
    if (!v) return 0;
    while (v >>= 1) ++n;
    return n;
}

static uint32_t table_nibble(const uint8_t *p, unsigned i)
{
    uint8_t v = p[i >> 1];
    return (i & 1u) ? (v >> 4) : (v & 0x0fu);
}

static uint32_t table_u16(const uint8_t *p, unsigned i)
{
    uint16_t v;
    memcpy(&v, p + i * 2u, sizeof(v));
    return v;
}

/* D28C0: interpolate between values at 2^n and 2^(n+1).
 * The ARM helper computes a Q16 slope and rounds the product by +0x8000
 * before the final >>16.  All known ISP callers use non-negative table data. */
static uint32_t stock_interp_pow2(uint32_t x, uint32_t x0, uint32_t x1,
                                  uint32_t y0, uint32_t y1)
{
    int64_t dy;
    int64_t slope_q16;
    int64_t acc;
    if (x1 == x0) return y0;
    dy = (int64_t)y1 - (int64_t)y0;
    slope_q16 = (dy * INT64_C(65536)) / (int64_t)(x1 - x0);
    acc = ((int64_t)x - (int64_t)x0) * slope_q16 + 0x8000;
    return (uint32_t)((int64_t)y0 + (acc >> 16));
}

/* Exact D2910 table-format dispatch.
 * type 0/3: packed nibbles; type 1/2: bytes; type 4: little-endian u16. */
static uint32_t stock_d2910(const uint8_t *table, uint32_t x, unsigned type)
{
    unsigned n = stock_log2_u32(x);
    unsigned i0 = n - 6u;
    unsigned i1 = n - 5u;
    uint32_t y0 = 0, y1 = 0;
    uint32_t x0 = 1u << n;
    uint32_t x1 = x0 << 1;

    if (!table || n < 6u || n >= 31u) return 0;
    switch (type) {
    case 0:
    case 3:
        y0 = table_nibble(table, i0);
        y1 = table_nibble(table, i1);
        break;
    case 1:
    case 2:
        y0 = table[i0];
        y1 = table[i1];
        break;
    case 4:
        y0 = table_u16(table, i0);
        y1 = table_u16(table, i1);
        break;
    default:
        break;
    }
    return stock_interp_pow2(x, x0, x1, y0, y1);
}

/* Exact D29C0 table-format dispatch.
 * type 1/2: bytes; type 4: little-endian u16; other types yield zero. */
static uint32_t stock_d29c0(const uint8_t *table, uint32_t x, unsigned type)
{
    unsigned n = stock_log2_u32(x);
    unsigned i0 = n - 6u;
    unsigned i1 = n - 5u;
    uint32_t y0 = 0, y1 = 0;
    uint32_t x0 = 1u << n;
    uint32_t x1 = x0 << 1;

    if (!table || n < 6u || n >= 31u) return 0;
    if (type == 1u || type == 2u) {
        y0 = table[i0];
        y1 = table[i1];
    } else if (type == 4u) {
        y0 = table_u16(table, i0);
        y1 = table_u16(table, i1);
    }
    return stock_interp_pow2(x, x0, x1, y0, y1);
}

/* C5DB8 success path. Stock treats ctx+0xa70 == 0 as an internal fatal state;
 * callers here return -EAGAIN instead of reproducing the vendor abort path. */
static int stock_gain_quantity(const struct fh_isp_runtime *rt, uint32_t *gain)
{
    uint32_t valid, v;
    if (!rt || !gain) return -EINVAL;
    memcpy(&valid, rt->ctx + 0x0a70, sizeof(valid));
    if (!valid) return -EAGAIN;
    memcpy(&v, rt->ctx + 0x0060, sizeof(v));
    *gain = v >> 12;
    return 0;
}

int fh_isp_runtime_attach_mmio(struct fh_isp_runtime *rt, volatile void *mmio, size_t size)
{
    uintptr_t p;
    if (!rt || !mmio || size < FH_ISP_MMIO_SIZE)
        return -EINVAL;
    rt->mmio = (volatile uint32_t *)mmio;
    rt->mmio_size = size;
    /* Stock context+0xa5c is the 32-bit ISP MMIO pointer.  The userspace ABI
     * on FH8626 is 32-bit ARM, so retain the exact field for future direct
     * translations while C code continues to use rt->mmio. */
    p = (uintptr_t)mmio;
    ctx_set_u32(rt, FH_ISP_MMIO_PTR_OFF, (uint32_t)p);
    return 0;
}

/* C6934/C6A9C resolve their live LTM objects inside the mapped isp_cfg VMM
 * allocation. Keep that mapping explicit instead of rediscovering it by
 * scanning for plausible statistics. */
int fh_isp_runtime_attach_isp_cfg(struct fh_isp_runtime *rt, volatile void *isp_cfg, size_t size)
{
    if (!rt || !isp_cfg || size < 0x2125cu)
        return -EINVAL;
    rt->isp_cfg = (volatile uint8_t *)isp_cfg;
    rt->isp_cfg_size = size;
    return 0;
}

/* API_ISP_SensorRegCb -> C1E2C -> memcpy(global_ctx + 0xc40, cb, 0x68). */
int fh_isp_runtime_register_sensor(struct fh_isp_runtime *rt, struct fh_sensor_gc1054 *sensor)
{
    if (!rt || !sensor || !sensor->cb)
        return -EINVAL;
    memcpy(rt->ctx + FH_ISP_SENSOR_CB_OFF, sensor->cb, FH_ISP_SENSOR_CB_SIZE);
    rt->sensor = sensor;
    return 0;
}

/* BF368, called by API_ISP_SetSensorFmt after the sensor get_vi_attr callback.
 * This is the missing pre-parameter context initialization.  LoadIspParam
 * intentionally preserves ctx[0x14..0x2b], so these fields must exist before
 * the 0xa58 SREG payload is loaded. */
int fh_isp_runtime_apply_vi_attr(struct fh_isp_runtime *rt, const void *vi_attr, size_t len)
{
    const uint8_t *p = (const uint8_t *)vi_attr;
    uint16_t h[8];
    uint32_t fmt;
    unsigned i;
    if (!rt || !vi_attr || len < 24)
        return -EINVAL;
    for (i = 0; i < 8; ++i)
        memcpy(&h[i], p + i * 2, 2);
    memcpy(&fmt, p + 20, 4);

    /* Exact BF368 field mapping. Existing high nibbles are preserved for the
     * six 12-bit geometry fields. */
    ctx_set_u16(rt, 0x1a, h[0]);
    ctx_set_u16(rt, 0x18, h[1]);
    ctx_set_u16(rt, 0x16, (uint16_t)((ctx_u16(rt, 0x16) & 0xf000u) | (h[2] & 0x0fffu)));
    ctx_set_u16(rt, 0x14, (uint16_t)((ctx_u16(rt, 0x14) & 0xf000u) | (h[3] & 0x0fffu)));
    ctx_set_u16(rt, 0x22, (uint16_t)((ctx_u16(rt, 0x22) & 0xf000u) | (h[6] & 0x0fffu)));
    ctx_set_u16(rt, 0x20, (uint16_t)((ctx_u16(rt, 0x20) & 0xf000u) | (h[7] & 0x0fffu)));
    ctx_set_u16(rt, 0x1c, (uint16_t)((ctx_u16(rt, 0x1c) & 0xf000u) | (h[4] & 0x0fffu)));
    ctx_set_u16(rt, 0x1e, (uint16_t)((ctx_u16(rt, 0x1e) & 0xf000u) | (h[5] & 0x0fffu)));

    rt->ctx[0x0e] = (uint8_t)((rt->ctx[0x0e] & ~0x38u) | ((fmt >> 1) & 0x38u));
    rt->ctx[0x0d] = (uint8_t)((rt->ctx[0x0d] & ~0x03u) | (fmt & 0x03u));

    rt->width = ctx_u16(rt, 0x14) & 0x0fffu;
    rt->height = ctx_u16(rt, 0x16) & 0x0fffu;
    return 0;
}

/* API_ISP_LoadIspParam / BEFB8 copy semantics. Stock's version mismatch
 * calls puts, then succeeds; it is NOT an assertion or a rejection gate.
 * Internal parameter context size is 0xa5c.  The loader intentionally keeps
 * [0x04..0x0f] and [0x14..0x2b], copies [0..3], [0x10..0x13], and [0x2c..end],
 * then sets config byte +0x367 bit7 as the runtime dirty flag.
 */
int fh_isp_runtime_load_param(struct fh_isp_runtime *rt, const void *profile, size_t len)
{
    const uint8_t *p = (const uint8_t *)profile;
    uint8_t tmp[FH_ISP_PARAM_SIZE];
    size_t n;
    if (!rt || !profile || len < 0x0a58u)
        return -EINVAL;

    /* Current-Ghidra DCCE4 -> DC3E8 allocates extent+0x14 using
     * 8F34C -> 132EC, the ELF-relocated calloc(size,1) import. The file
     * path reads min(extent,descriptor_size) at buffer+0x10. Thus an A58
     * SREG payload leaves four zero bytes for C1EB0's A5C extent. The
     * DC350 cache/full-buffer path can supply real tail bytes: preserve
     * those when provided, never read beyond the caller's payload. */
    memset(tmp, 0, sizeof(tmp));
    n = len < sizeof(tmp) ? len : sizeof(tmp);
    memcpy(tmp, p, n);
    p = tmp;

    memcpy(rt->ctx + 0x00, p + 0x00, 4);
    memcpy(rt->ctx + 0x10, p + 0x10, 4);
    memcpy(rt->ctx + 0x2c, p + 0x2c, FH_ISP_PARAM_SIZE - 0x2c);
    rt->ctx[0x367] |= 0x80;
    rt->params_dirty = 1;
    /* C73F8 keeps both history bands in module-static storage outside the
     * 0xa5c parameter context copied by BEFB8.  A profile load therefore must
     * not clear them.  On the first load only, wait selector+1 publications:
     * C73F8 inserts the fresh callback sample at selector and shifts it one
     * slot toward index zero on each subsequent publication. */
    {
        int first_profile = rt->profile_generation == 0u;
        rt->profile_generation++;
        if (!rt->profile_generation) rt->profile_generation = 1u;
        if (first_profile) {
            rt->temporal_generation = 0u;
            rt->nr3d_warmup_left = (rt->ctx[0x3au] & 7u) + 1u;
        } else {
            rt->temporal_generation = rt->profile_generation;
            rt->nr3d_warmup_left = 0u;
        }
    }
    return 0;
}

void fh_isp_runtime_set_nr3d_enabled(struct fh_isp_runtime *rt, int enabled)
{
    if (!rt) return;
    rt->nr3d_enabled = !!enabled;
    if (enabled) rt->ctx[0x11] |= 0x40u;
    else rt->ctx[0x11] &= (uint8_t)~0x40u;
}

void fh_isp_runtime_accept_stats_epoch(struct fh_isp_runtime *rt, uint32_t epoch)
{
    if (rt && epoch) rt->source_stats_epoch = epoch;
}

void fh_isp_runtime_prepare_nr3d_reenable(struct fh_isp_runtime *rt)
{
    if(!rt)return;
    rt->temporal_generation=0;
    rt->nr3d_warmup_epoch=rt->source_stats_epoch;
    rt->nr3d_warmup_left=(rt->ctx[0x3a]&7u)+1u;
}

/* Exact C6D04 ordering: aggregate current error plus OLD slots 0..58, shift
 * old slots 1..59 toward zero, then install current error in slot 59. */
int fh_isp_runtime_publish_control_metric(struct fh_isp_runtime *rt,
                                          uint32_t epoch,
                                          int32_t measured,
                                          int32_t target)
{
    int64_t sum;
    unsigned i;
    if (!rt || !epoch) return -EINVAL;
    if (epoch != rt->source_stats_epoch) return -ESTALE;
    if (epoch == rt->control_metric_epoch) return 0;
    rt->control_delta = measured - target;
    sum = rt->control_delta;
    for (i = 0; i < 59u; ++i) sum += rt->control_delta_history[i];
    for (i = 0; i < 59u; ++i)
        rt->control_delta_history[i] = rt->control_delta_history[i + 1u];
    rt->control_delta_history[59] = rt->control_delta;
    rt->control_delta_aggregate = (int32_t)sum;
    rt->control_metric_epoch = epoch;
    return 0;
}

static uint32_t stock_isqrt_u32(uint32_t value)
{
    uint32_t root = 0u, bit = 1u << 30;
    while (bit > value) bit >>= 2;
    while (bit) {
        if (value >= root + bit) {
            value -= root + bit;
            root = (root >> 1) + bit;
        } else root >>= 1;
        bit >>= 2;
    }
    return root;
}

/* C757C exact seven-way weighting topology recovered from the current Ghidra
 * instructions, including the computed jump at C75C0. Modes 3/4 select the
 * N+1 darkest/brightest groups after C6C44's ascending sort; mode 6 selects
 * one sorted group. The caller supplies C6C00's first nine-word snapshot. */
int fh_isp_runtime_apply_c757c_metric(struct fh_isp_runtime *rt,
                                      const uint32_t group_values[9])
{
    uint32_t weights[9] = {0}, order[9];
    uint64_t weighted = 0u;
    uint32_t weight_sum = 0u, mean, metric;
    unsigned i, j, mode, n;
    if (!rt || !group_values) return -EINVAL;
    if (group_values != rt->c757c_group_value)
        memcpy(rt->c757c_group_value, group_values,
               sizeof(rt->c757c_group_value));
    for (i = 0u; i < 9u; ++i) order[i] = i;
    for (i = 0u; i + 1u < 9u; ++i) {
        unsigned lowest = i;
        for (j = i + 1u; j < 9u; ++j)
            if (group_values[order[j]] < group_values[order[lowest]]) lowest = j;
        if (lowest != i) {
            uint32_t tmp = order[i]; order[i] = order[lowest]; order[lowest] = tmp;
        }
    }
    mode = rt->ctx[0x3du];
    n = rt->ctx[0x3eu] & 0x0fu;
    switch (mode) {
    case 1u:
        weights[4] = 0xffu;
        break;
    case 2u:
        for (i = 0u; i < 9u; ++i) weights[i] = 0x40u;
        weights[4] = 0xffu;
        break;
    case 3u:
        if (n > 8u) n = 8u;
        for (i = 0u; i <= n; ++i) weights[order[i]] = 0xffu;
        break;
    case 4u:
        if (n > 8u) n = 8u;
        for (i = 0u; i <= n; ++i) weights[order[8u - i]] = 0xffu;
        break;
    case 5u:
        for (i = 0u; i < 9u; ++i) weights[i] = rt->ctx[0x4cu + i];
        break;
    case 6u:
        if (n > 8u) n = 8u;
        weights[order[n]] = 0xffu;
        break;
    case 0u:
    default:
        for (i = 0u; i < 9u; ++i) weights[i] = 0xffu;
        break;
    }
    for (i = 0u; i < 9u; ++i) {
        weight_sum += weights[i];
        weighted += (uint64_t)weights[i] * group_values[i];
    }
    if (!weight_sum) weight_sum = 1u;
    mean = (uint32_t)(weighted / weight_sum);
    if (mean > 0x0fffu) mean = 0x1000u;
    metric = stock_isqrt_u32(mean << 12);
    if (!metric) metric = 1u;
    else if (metric > 0x0fffu) metric = 0x1000u;
    rt->c757c_weighted_mean = mean;
    rt->c757c_metric_q12 = metric;
    rt->control_target_q12 = (uint32_t)rt->ctx[0x30] << 4;
    {
        uint16_t packed;
        memcpy(&packed, rt->ctx + 0x58u, sizeof(packed));
        packed = (uint16_t)((packed & 0xf000u) | (metric & 0x0fffu));
        memcpy(rt->ctx + 0x58u, &packed, sizeof(packed));
    }
    return 0;
}

/* C670C/C6960/C6C00 exact 16x16 tile topology. Each source tile is four
 * words; C6960 consumes words 0/1 from four planes separated by 0x1000 bytes.
 * The nine overlapping 6x6/5x5 regions below are the runtime-initialized
 * index arrays recovered at Apollo VA 0x317a78 and verified against C670C. */
int fh_isp_runtime_snapshot_c6c00(struct fh_isp_runtime *rt)
{
    static const uint8_t region[9][4] = {
        {0,0,6,6}, {6,0,5,5}, {11,0,5,5},
        {0,6,5,5}, {5,5,6,6}, {11,5,5,5},
        {0,11,5,5}, {5,11,5,5}, {10,10,6,6}
    };
    volatile const uint32_t *base;
    unsigned g, y, x, plane;
    if (!rt || !rt->isp_cfg || rt->isp_cfg_size < 0x45c8u) return -ENODATA;
    base = (volatile const uint32_t *)(rt->isp_cfg + 0x5c8u);
    for (g = 0u; g < 9u; ++g) {
        uint32_t sum = 0u, count = 0u;
        for (y = region[g][1]; y < (unsigned)region[g][1] + region[g][3]; ++y) {
            for (x = region[g][0]; x < (unsigned)region[g][0] + region[g][2]; ++x) {
                unsigned tile = y * 16u + x;
                for (plane = 0u; plane < 4u; ++plane) {
                    const volatile uint32_t *p = base + plane * 0x400u + tile * 4u;
                    sum += p[0];
                    count += p[1];
                }
            }
        }
        rt->ltm_group_sum[g] = sum;
        rt->ltm_group_count[g] = count;
    }
    return 0;
}

int fh_isp_runtime_de6cc_scene_sample(const struct fh_isp_runtime *rt,
                                      uint32_t *sample)
{
    /* Exact DE6CC weights multiplied by two:
     *   0.5 1.0 0.5 / 1.0 3.0 1.0 / 0.5 1.0 0.5
     * Stock divides their weighted mean by the BFD9C total-gain snapshot,
     * whose unity representation is 64. */
    static const uint8_t weight_x2[9] = {1,2,1,2,6,2,1,2,1};
    uint64_t weighted = 0u;
    uint32_t gain;
    unsigned i;
    if (!rt || !sample) return -EINVAL;
    for (i = 0u; i < 9u; ++i)
        weighted += (uint64_t)rt->ltm_group_sum[i] * weight_x2[i];
    gain = ctx_u32(rt, 0x60u) >> 12;
    if (!gain) gain = 64u;
    *sample = (uint32_t)((weighted / 18u) / gain);
    if (!*sample) return -ENODATA;
    if (*sample > 4096u) return -ERANGE;
    return 0;
}

int fh_isp_runtime_nr3d_ready(const struct fh_isp_runtime *rt)
{
    return rt && rt->nr3d_enabled && !rt->nr3d_warmup_left &&
           ctx_u32(rt,0x11acu) == 1u && /* CB970: driver configuration, not opt-in */
           (rt->ctx[0x11]&0x40u) && ctx_u32(rt,0xa70u) &&
           rt->profile_generation && rt->source_stats_epoch &&
           rt->temporal_generation == rt->profile_generation &&
           rt->gain_epoch == rt->source_stats_epoch &&
           rt->gain_epoch == rt->control_epoch;
}

int fh_isp_runtime_load_sreg_profile(struct fh_isp_runtime *rt,
                                    const void *container, size_t len,
                                    const char *profile_name)
{
    const uint8_t *p = (const uint8_t *)container;
    uint32_t count, off, size;
    unsigned i;
    if (!rt || !p || !profile_name) return -EINVAL;
    if (len < 0x3cu || memcmp(p, "SREG", 4u) != 0) return -EINVAL;
    memcpy(&count, p + 0x38u, sizeof(count));
    if (count == 0u || count > 32u || len < 0x3cu + (size_t)count * 24u)
        return -EINVAL;
    for (i = 0; i < count; ++i) {
        const uint8_t *d = p + 0x3cu + (size_t)i * 24u;
        size_t name_len = 0u;
        while (name_len < 16u && d[name_len] != 0u) ++name_len;
        if (strlen(profile_name) != name_len ||
            memcmp(d, profile_name, name_len) != 0)
            continue;
        memcpy(&off, d + 16u, sizeof(off));
        memcpy(&size, d + 20u, sizeof(size));
        if (size != FH_ISP_SREG_PROFILE_SIZE || off > len || size > len - off)
            return -EINVAL;
        return fh_isp_runtime_load_param(rt, p + off, size);
    }
    return -ENOENT;
}

/* Exact positive-dimension C45FC geometry derivation.  The stock routine is
 * written for signed inputs, but API_ISP_SetSensorFmt masks all dimensions to
 * 12 bits before C531C, so valid camera geometry is strictly positive here. */
static int apply_c45fc(struct fh_isp_runtime *rt, unsigned w, unsigned h)
{
    uint32_t wh, w6, h6, c, v;
    uint32_t w16, w8, w4, w3_16, w5_16, w3_8, w7_16;
    uint32_t h8, h4, h3_8;
    if (!rt || !rt->mmio || !w || !h || w > 0x0fffu || h > 0x0fffu)
        return -EINVAL;

    wh = ((h - 1u) << 16) | (w - 1u);
    c = w >> 1;
    w6 = w / 6u;
    h6 = h / 6u;
    if (!w6 || !h6 || w < 48u || h < 32u)
        return -EINVAL;

    w16 = w >> 4;
    w8 = w >> 3;
    w4 = w >> 2;
    w3_16 = (3u * c) >> 3;
    w5_16 = (5u * c) >> 3;
    w3_8 = (6u * c) >> 3;
    w7_16 = (7u * c) >> 3;
    h8 = h >> 3;
    h4 = h >> 2;
    h3_8 = (3u * h) >> 3;

    wr(rt, 0x080, wh);
    wr(rt, 0x490, (((5u * w) >> 2) + 7u) & ~7u);
    wr(rt, 0x488, (w4 + 7u) & ~7u);
    wr(rt, 0x1ec, ((h / 32u - 1u) << 24) | ((w / 48u - 1u) << 16));
    wr(rt, 0x5c0, 0);
    wr(rt, 0x5c4, wh);

    v = ((w6 - 1u) << 16);
    wr(rt, 0x17c, v);
    wr(rt, 0x180, ((3u * w6 - 1u) << 16) | (2u * w6 - 1u));
    wr(rt, 0x184, (h6 - 1u) << 16);
    wr(rt, 0x188, ((3u * h6 - 1u) << 16) | (2u * h6 - 1u));
    wr(rt, 0x1a4, v);
    wr(rt, 0x1a8, ((3u * w6 - 1u) << 16) | (2u * w6 - 1u));
    wr(rt, 0x1ac, (h6 - 1u) << 16);
    wr(rt, 0x1b0, ((3u * h6 - 1u) << 16) | (2u * h6 - 1u));

    v = 0x0f0f0000u | ((h / 32u - 1u) << 8) | (w / 32u - 1u);
    wr(rt, 0x1cc, v);
    wr(rt, 0x1d0, 0);
    wr(rt, 0x1d4, 0x0001fff1u);
    wr(rt, 0x1dc, v);
    wr(rt, 0x1e0, 0);
    wr(rt, 0x1e4, 0x0001fff1u);

    wr(rt, 0x38c, w16 << 16);
    wr(rt, 0x390, (w3_16 << 16) | w8);
    wr(rt, 0x394, (w5_16 << 16) | w4);
    wr(rt, 0x398, (w7_16 << 16) | w3_8);
    wr(rt, 0x39c, c - 1u);
    wr(rt, 0x3a0, (h4 << 16) | h8);
    wr(rt, 0x3a4, (((h >> 1) - 1u) << 16) | h3_8);
    wr(rt, 0x3a8, 0x0fff0000u);
    return 0;
}

/* Full C531C geometry path: C4894(ctx+0x14/+0x16),
 * C45FC(ctx+0x20/+0x22), C48B0(ctx+0x1c/+0x1e). */
int fh_isp_runtime_apply_c531c(struct fh_isp_runtime *rt)
{
    unsigned w0, h0, w1, h1, w2, h2;
    uint32_t wh;
    int rc;
    if (!rt || !rt->mmio) return -EINVAL;
    w0 = ctx_u16(rt, 0x14) & 0x0fffu; h0 = ctx_u16(rt, 0x16) & 0x0fffu;
    w2 = ctx_u16(rt, 0x1c) & 0x0fffu; h2 = ctx_u16(rt, 0x1e) & 0x0fffu;
    w1 = ctx_u16(rt, 0x20) & 0x0fffu; h1 = ctx_u16(rt, 0x22) & 0x0fffu;
    if (!w0 || !h0 || !w1 || !h1) return -EINVAL;

    wh = ((h0 - 1u) << 16) | (w0 - 1u);
    wr(rt, 0x078, wh);
    wr(rt, 0x030, wh);
    rc = apply_c45fc(rt, w1, h1); if (rc) return rc;
    wr(rt, 0x07c, (h2 << 16) | w2);
    rt->width = (uint16_t)w0; rt->height = (uint16_t)h0;
    return fh_isp_runtime_apply_format_bits(rt);
}

/* Compatibility helper retained for callers that only know width/height.
 * It seeds both C4894 and C45FC input pairs, then runs the exact C531C path. */
int fh_isp_runtime_apply_geometry(struct fh_isp_runtime *rt, unsigned width, unsigned height)
{
    if (!rt || !width || !height || width > 0x0fffu || height > 0x0fffu) return -EINVAL;
    ctx_set_u16(rt, 0x14, (uint16_t)width);
    ctx_set_u16(rt, 0x16, (uint16_t)height);
    ctx_set_u16(rt, 0x20, (uint16_t)width);
    ctx_set_u16(rt, 0x22, (uint16_t)height);
    return fh_isp_runtime_apply_c531c(rt);
}

/* C531C: translate sensor/VI format fields from the internal 0xa5c config into
 * ISP +0x024/+0x3ac/+0x3b4.  This is exact bit logic from disassembly.
 * param[0x0d] low 2 bits are the input-format selector; param[0x0e] bits 3..5
 * supply the paired format field.
 */
int fh_isp_runtime_apply_format_bits(struct fh_isp_runtime *rt)
{
    uint32_t v24, v3ac, v3b4;
    uint32_t f0, f1;
    if (!rt || !rt->mmio)
        return -EINVAL;

    f0 = rt->ctx[0x0d] & 3u;
    f1 = (rt->ctx[0x0e] >> 3) & 7u;

    v24 = rd(rt, 0x024);
    v24 = (v24 & ~0x06u) | (f0 << 1);
    v24 = (v24 & ~0x18u) | (f0 << 3);
    wr(rt, 0x024, v24);

    v3b4 = rd(rt, 0x3b4);
    v3ac = rd(rt, 0x3ac);
    v3b4 &= ~0x0000000fu;
    v3b4 |= f1;
    v3b4 &= ~0x0000c000u;
    v3b4 |= f0 << 14;
    v3ac &= ~0x00000007u;
    v3ac |= f1;
    wr(rt, 0x3b4, v3b4);
    wr(rt, 0x3ac, v3ac);
    return 0;
}

/* C53E0: final core-enable state at the end of C540C. */
int fh_isp_runtime_finish_core_init(struct fh_isp_runtime *rt)
{
    uint32_t v;
    if (!rt || !rt->mmio)
        return -EINVAL;
    wr(rt, 0x018, 1);
    v = rd(rt, 0x068);
    wr(rt, 0x068, v | 1u);
    return 0;
}

/*
 * This is deliberately named PROVEN SUBSET.  It is not yet a replacement for
 * C4998/C48CC or the post-C540C C1AF4/C64B4/C5524 stages.
 *
 * It provides a safe integration boundary for the daemon while reverse of the
 * remaining LUT/default writers continues.  Do not treat this as final image
 * quality initialization.
 */
int fh_isp_runtime_init_proven_subset(struct fh_isp_runtime *rt, unsigned width, unsigned height)
{
    int rc;
    if (!rt || !rt->mmio)
        return -EINVAL;

    /* C4998 establishes this literal before C531C rewrites format bits. */
    wr(rt, 0x024, 0x0a71eb14u);

    rc = fh_isp_runtime_apply_geometry(rt, width, height);
    if (rc) return rc;
    rc = fh_isp_runtime_apply_format_bits(rt);
    if (rc) return rc;
    return fh_isp_runtime_finish_core_init(rt);
}



/* CE430: stock BLC quartet writer.  The historical reverse map incorrectly
 * associated CE430 with +0x4c0..+0x4d4; those registers are CE764/CE670/CE628.
 * CE430 itself writes ISP +0x084/+0x088 from four channel values. */
int fh_isp_runtime_apply_ce430(struct fh_isp_runtime *rt)
{
    static const uint8_t bayer_perm[4][4] = {
        { 0, 1, 3, 2 },
        { 1, 0, 2, 3 },
        { 3, 2, 0, 1 },
        { 2, 3, 1, 0 },
    };
    uint16_t ch[4], base16;
    uint32_t gain, base, mode;
    unsigned sel, i;
    int rc;

    if (!rt || !rt->mmio) return -EINVAL;
    if (!(rt->ctx[0x11] & 0x01u)) return 0;

    rc = stock_gain_quantity(rt, &gain);
    if (rc) return rc;

    mode = rt->ctx[0x158];
    memcpy(&base16, rt->ctx + 0x15a, sizeof(base16));
    base = base16 & 0x0fffu;

    if (mode & 0x01u)
        base = stock_d29c0(rt->ctx + 0x160, gain, 4u) & 0xffffu;

    if (mode & 0x02u) {
        if (mode & 0x01u) {
            uint16_t t0[12], t1[12], t2[12];
            for (i = 0; i < 12u; ++i) {
                int32_t a = (int8_t)rt->ctx[0x178u + i] + (int32_t)base;
                int32_t b = (int8_t)rt->ctx[0x184u + i] + (int32_t)base;
                int32_t c = (int8_t)rt->ctx[0x190u + i] + (int32_t)base;
                t0[i] = (uint16_t)(a < 0 ? 0 : a);
                t1[i] = (uint16_t)(b < 0 ? 0 : b);
                t2[i] = (uint16_t)(c < 0 ? 0 : c);
            }
            ch[0] = (uint16_t)stock_d29c0((const uint8_t *)t0, gain, 4u);
            ch[1] = (uint16_t)stock_d29c0((const uint8_t *)t1, gain, 4u);
            ch[2] = (uint16_t)stock_d29c0((const uint8_t *)t2, gain, 4u);
        } else {
            ch[0] = (uint16_t)(base + (int8_t)rt->ctx[0x15c]);
            ch[1] = (uint16_t)(base + (int8_t)rt->ctx[0x15d]);
            ch[2] = (uint16_t)(base + (int8_t)rt->ctx[0x15e]);
        }
        ch[0] &= 0xffffu; ch[1] &= 0xffffu; ch[2] &= 0xffffu;
    } else {
        ch[0] = (uint16_t)base;
        ch[1] = (uint16_t)base;
        ch[2] = (uint16_t)base;
    }
    ch[3] = ch[1];

    sel = rt->ctx[0x0d] & 3u;
    {
        uint16_t p0 = ch[bayer_perm[sel][0]];
        uint16_t p1 = ch[bayer_perm[sel][1]];
        uint16_t p2 = ch[bayer_perm[sel][2]];
        uint16_t p3 = ch[bayer_perm[sel][3]];
        wr(rt, 0x084, (uint32_t)p0 | ((uint32_t)p1 << 16));
        wr(rt, 0x088, (uint32_t)p2 | ((uint32_t)p3 << 16));
    }
    return 0;
}

/* D0E5C: source-derived gain-dependent writer in the stock CB970 chain.
 * Active when ctx[0x11] is negative as signed byte.  It writes one packed
 * value to ISP +0x454..+0x460 and updates low 16 bits of +0x464.
 * Dynamic scalar mode uses C5DB8 + D2910(type=1) from ctx+0x244. */
int fh_isp_runtime_apply_d0e5c(struct fh_isp_runtime *rt)
{
    int16_t quad_coeff, quad_bias;
    int64_t q, mixed;
    uint32_t gain, scalar, base, packed, v464;
    int32_t signed20;
    uint32_t byte8;
    int rc;

    if (!rt || !rt->mmio) return -EINVAL;
    if ((int8_t)rt->ctx[0x11] >= 0) return 0;

    memcpy(&quad_coeff, rt->ctx + 0x23c, sizeof(quad_coeff));
    memcpy(&quad_bias,  rt->ctx + 0x23e, sizeof(quad_bias));

    rc = stock_gain_quantity(rt, &gain);
    if (rc) return rc;

    if (rt->ctx[0x234] & 0x01u)
        scalar = stock_d2910(rt->ctx + 0x244, gain, 1);
    else
        scalar = rt->ctx[0x238];

    /* byte field: scalar * (ctx[23a] + round(gain*ctx[239]/4096)), Q4. */
    base = (uint32_t)rt->ctx[0x23a] +
           (uint32_t)(((uint64_t)gain * rt->ctx[0x239] + 0x800u) >> 12);
    byte8 = (scalar * base + 8u) >> 4;
    if (byte8 > 0xffu) byte8 = 0xffu;

    /* signed 20-bit field: scalar * (bias + round(coeff*gain^2/2^22)), Q4. */
    q = (int64_t)quad_coeff * (int64_t)(int32_t)gain * (int64_t)(int32_t)gain;
    q = (q + 0x200000ll) >> 22;
    q += quad_bias;
    mixed = q * (int64_t)(int32_t)scalar;
    signed20 = (int32_t)((mixed + 8ll) >> 4);
    if (signed20 > 0x7ffff) signed20 = 0x7ffff;
    if (signed20 < -0x80000) signed20 = -0x80000;

    packed = ((uint32_t)(rt->ctx[0x240] & 0x0fu) << 28) |
             (((uint32_t)signed20 & 0x000fffffu) << 8) |
             (byte8 & 0xffu);
    wr(rt, 0x454, packed);
    wr(rt, 0x458, packed);
    wr(rt, 0x45c, packed);
    wr(rt, 0x460, packed);

    v464 = rd(rt, 0x464);
    v464 = (v464 & 0xffff0000u) |
           ((uint32_t)rt->ctx[0x241] << 8) |
           (uint32_t)rt->ctx[0x235];
    wr(rt, 0x464, v464);
    return 0;
}



static int32_t stock_sx13(uint16_t v)
{
    uint32_t x = (uint32_t)v & 0x1fffu;
    return (x & 0x1000u) ? (int32_t)(x | 0xffffe000u) : (int32_t)x;
}

static int32_t stock_round_q8_signed(int32_t x)
{
    int32_t t = x + 128;
    if (t < 0) t += 255;
    return t >> 8;
}

/* CE764 -> CE670 + CE628.  Stock order: after D0E5C, before CFD70. */
int fh_isp_runtime_apply_ce764(struct fh_isp_runtime *rt)
{
    uint32_t w, iw, ia, ib;
    const uint8_t *a, *b;
    unsigned i;
    if (!rt || !rt->mmio) return -EINVAL;
    if (!(rt->ctx[0x12] & 0x01u)) return 0;

    memcpy(&w, rt->ctx + 0xb0, 2u);
    w &= 0x0fffu;
    /* CE670 masks twelve bits without clamping to256. Larger configured
     * weights extrapolate with a signed (256-w) term in the ARM word math. */
    iw = 256u - w;
    ia = rt->ctx[0xb2] & 0x0fu;
    ib = rt->ctx[0xb1] >> 4;
    a = rt->ctx + 0xe0u + ia * 24u;
    b = rt->ctx + 0xe0u + ib * 24u;

    for (i = 0; i < 6u; ++i) {
        uint16_t a0u, a1u, b0u, b1u;
        int32_t a0, a1, b0, b1, o0, o1;
        int64_t m0, m1;
        memcpy(&a0u, a + i * 4u, 2u);
        memcpy(&a1u, a + i * 4u + 2u, 2u);
        memcpy(&b0u, b + i * 4u, 2u);
        memcpy(&b1u, b + i * 4u + 2u, 2u);
        a0 = stock_sx13(a0u); a1 = stock_sx13(a1u);
        b0 = stock_sx13(b0u); b1 = stock_sx13(b1u);
        m0 = (int64_t)(int32_t)w * a0 + (int64_t)(int32_t)iw * b0;
        m1 = (int64_t)(int32_t)w * a1 + (int64_t)(int32_t)iw * b1;
        o0 = stock_round_q8_signed((int32_t)m0);
        o1 = stock_round_q8_signed((int32_t)m1);
        ctx_set_u32(rt, 0x140u + i * 4u,
            ((uint32_t)o0 & 0x1fffu) | (((uint32_t)o1 & 0x1fffu) << 16));
    }
    /* CE670 completes its context loop before CE628 publishes all six words.
     * Preserve in-place selector aliasing, not a detached coefficient array. */
    for (i = 0; i < 6u; ++i)
        wr(rt, 0x4c0u + i * 4u, ctx_u32(rt, 0x140u + i * 4u));
    return 0;
}

static void stock_cfa28(struct fh_isp_runtime *rt, unsigned which)
{
    if (which == 1u)
        memmove(rt->ctx + 0x5f0u, rt->ctx + 0x370u, 80u * sizeof(uint32_t));
    else
        memmove(rt->ctx + 0x730u, rt->ctx + 0x4b0u, 80u * sizeof(uint32_t));
}

static int stock_cf8ec(struct fh_isp_runtime *rt, unsigned output_bank,
                       unsigned preset_index)
{
    uint8_t *dst;
    /* CF908 -> CF964 BX LR: an unsupported preset is a local no-op, not
     * an error that prevents the other gamma bank and pending publication. */
    if (preset_index > 9u) return 0;
    dst = rt->ctx + (output_bank == 1u ? 0x5f0u : 0x730u);
    memcpy(dst, fh_cf8ec_gamma_presets[preset_index], 0x140u);
    return 0;
}

static uint16_t stock_cedc0_le16(size_t off)
{
    return (uint16_t)fh_cedc0_tables[off] |
           (uint16_t)((uint16_t)fh_cedc0_tables[off + 1u] << 8);
}

/* CEDC0: exact selector topology and arithmetic from current Ghidra.  The
 * static curve/shaping payload is the relocated stock RAM image whose GOT
 * owners and access widths were reconciled against CEDC0's instructions. */
static int stock_cedc0(struct fh_isp_runtime *rt, unsigned output_selector,
                       unsigned input_selector, unsigned curve_selector,
                       unsigned correction_selector, unsigned tuning_selector,
                       unsigned transform_selector)
{
    static const float tuning[5] = { 0.3f, 0.4f, 0.5f, 0.6f, 0.7f };
    static const double transform[5] = { 0.5, 0.6, 0.7, 0.8, 0.9 };
    static const double correction[9] =
        { 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9 };
    uint16_t lut[81] = {0};
    uint32_t packed[41];
    uint32_t threshold, start, x;
    float scale;
    unsigned i;

    if (output_selector > 1u || input_selector > 8u || curve_selector > 8u ||
        correction_selector > 9u || tuning_selector > 4u ||
        transform_selector > 4u)
        return -ERANGE;

    scale = tuning[tuning_selector];
    threshold = (uint32_t)(scale * 4095.0f);
    start = ((threshold >> 7) + 1u) << 7;

    /* Stock uses three nonuniform x ranges to build entries 16..knee. */
    for (x = 0x80u; x != 0x100u; x += 8u) {
        uint32_t y = curve_selector == 0u
            ? (uint32_t)((float)x * scale)
            : stock_cedc0_le16((curve_selector - 1u) * 1024u +
                               (((uint32_t)((float)x / scale) >> 3) * 2u));
        lut[x >> 3] = (uint16_t)y;
    }
    for (; x != 0x400u; x += 0x20u) {
        uint32_t y = curve_selector == 0u
            ? (uint32_t)((float)x * scale)
            : stock_cedc0_le16((curve_selector - 1u) * 1024u +
                               (((uint32_t)((float)x / scale) >> 3) * 2u));
        lut[(x >> 5) + 0x18u] = (uint16_t)y;
    }
    for (; x <= threshold; x += 0x80u) {
        uint32_t y = curve_selector == 0u
            ? (uint32_t)((float)x * scale)
            : stock_cedc0_le16((curve_selector - 1u) * 1024u +
                               (((uint32_t)((float)x / scale) >> 3) * 2u));
        lut[(x >> 7) + 0x30u] = (uint16_t)y;
    }

    for (i = 16u; i < 80u; ++i)
        lut[i] = (uint16_t)((double)lut[i] * transform[transform_selector]);

    if (input_selector == 0u) {
        for (i = 0u; i < 16u; ++i)
            lut[i] = (uint16_t)((double)(i * 8u) * (double)lut[16]);
    } else {
        size_t shape = 8192u + (input_selector - 1u) * 32u;
        for (i = 0u; i < 16u; ++i)
            lut[i] = (uint16_t)(((uint32_t)lut[16] *
                                 stock_cedc0_le16(shape + i * 2u)) >> 7);
    }

    if (start <= 0x1000u) {
        uint16_t knee = lut[(threshold >> 7) + 0x30u];
        for (x = start; x <= 0x1000u; x += 0x80u) {
            uint32_t y;
            if (correction_selector < 9u)
                y = (uint32_t)((double)(x - start) *
                               correction[correction_selector]) + knee;
            else
                y = x + (uint16_t)(knee - start);
            lut[(x >> 7) + 0x30u] = (uint16_t)y;
        }
        for (x = start; x <= 0x1000u; x += 0x80u) {
            unsigned p = (x >> 7) + 0x30u;
            if (lut[p] > 0x0fffu) lut[p] = 0x0fffu;
        }
    }

    for (i = 0u; i < 40u; ++i)
        packed[i] = (uint32_t)lut[i * 2u] |
                    ((uint32_t)lut[i * 2u + 1u] << 16);
    packed[40] = lut[80];

    if (output_selector == 0u) {
        for (i = 0u; i < 41u; ++i) {
            wr(rt, 0x1000u + i * 4u, packed[i]);
            wr(rt, 0x1180u + i * 4u, packed[i]);
            wr(rt, 0x1300u + i * 4u, packed[i]);
            memcpy(rt->ctx + 0x4b0u + i * 4u, &packed[i], 4u);
        }
    } else {
        for (i = 0u; i < 41u; ++i) {
            uint32_t v = (packed[i] >> 2) & 0xffff3fffu;
            wr(rt, 0x1480u + i * 4u, v);
            memcpy(rt->ctx + 0x370u + i * 4u, &v, 4u);
        }
    }
    return 0;
}

static void stock_cfa94(struct fh_isp_runtime *rt, unsigned mode)
{
    uint32_t v = rd(rt, 0x4f0);
    if (mode == 0u) {
        rt->gamma_meta0 = 0u; rt->gamma_meta1 = 0u;
    } else if (mode == 1u) {
        rt->gamma_meta0 = 1u; rt->gamma_meta1 = 1u; v &= ~0x10u;
    } else if (mode == 2u) {
        rt->gamma_meta0 = 1u; rt->gamma_meta1 = 0u; v |= 0x10u;
    } else if (mode == 3u) {
        rt->gamma_meta0 = 1u; rt->gamma_meta1 = 1u; v |= 0x10u;
    } else return;
    wr(rt, 0x4f0, v);
}

/* CFBC4 profile-driven gamma dispatcher. */
int fh_isp_runtime_apply_cfbc4(struct fh_isp_runtime *rt)
{
    unsigned a, b, c;
    int rc, first_error = 0;
    if (!rt || !rt->mmio) return -EINVAL;
    if (!(rt->ctx[0x12] & 0x40u)) return 0;
    if (!(rt->ctx[0x367] & 0x80u) && rt->params_dirty == 0) return 0;

    a = rt->ctx[0x365] & 3u;
    b = (rt->ctx[0x365] >> 4) & 3u;
    c = (rt->ctx[0x365] >> 2) & 3u;
    stock_cfa94(rt, a);
    if (b == 3u) stock_cfa28(rt, 0u);
    else if (b == 0u) {
        rc = stock_cf8ec(rt, b, rt->ctx[0x364] >> 4);
        if (rc) first_error = rc;
    }
    else if (b == 1u) {
        rc = stock_cedc0(rt, 0u, rt->ctx[0x36c] & 0x0fu,
                         rt->ctx[0x36c] >> 4, rt->ctx[0x36d] & 0x0fu,
                         rt->ctx[0x36d] >> 4, rt->ctx[0x36e] & 0x0fu);
        if (rc) first_error = rc;
    }
    if (c == 3u) stock_cfa28(rt, 1u);
    else if (c == 0u) {
        rc = stock_cf8ec(rt, 1u, rt->ctx[0x364] & 0x0fu);
        if (rc && !first_error) first_error = rc;
    }
    /* CFBC4 publishes its bank pointer after both independent selectors.
     * Retain locally rejected composer output, but do not skip the other bank. */
    rt->gamma_candidate_pending = 1;
    return first_error;
}

/* D2074: exact active GC1054 day-profile path.
 * Enable: ctx[0x13] bit0.  The current day profile has ctx[0x870]=0x05,
 * selecting both D2910 dynamic scalar/table paths.  This implementation also
 * retains the static branches visible in stock, while deliberately avoiding
 * any external/snapshot data.  Outputs: +4f4/+4f8/+4fc/+514/+518/+51c and
 * the packed 24-register coefficient block +a0c..+a68. */
static void stock_d2074_unpack5(const uint8_t *src, uint32_t scale, uint32_t out[15])
{
    unsigned i;
    for (i = 0; i < 5u; ++i) {
        uint32_t w;
        uint32_t f0, f1, f2;
        memcpy(&w, src + i * 4u, sizeof(w));
        f0 = w & 0x03ffu;
        f1 = (w >> 10) & 0x03ffu;
        f2 = (w >> 20) & 0x03ffu;
        out[i * 3u + 0u] = (f0 * scale) >> 6;
        out[i * 3u + 1u] = (f1 * scale) >> 6;
        out[i * 3u + 2u] = (f2 * scale) >> 6;
    }
}

static void stock_d2074_pack16(struct fh_isp_runtime *rt, unsigned off,
                               const uint32_t v[15], uint32_t tail)
{
    unsigned i;
    for (i = 0; i < 7u; ++i)
        wr(rt, off + i * 4u, (v[i * 2u] & 0xffffu) |
                             ((v[i * 2u + 1u] & 0xffffu) << 16));
    wr(rt, off + 28u, (v[14] & 0xffffu) | ((tail & 0xffffu) << 16));
}

int fh_isp_runtime_apply_d2074(struct fh_isp_runtime *rt)
{
    uint32_t mode, gain, scalar;
    uint32_t c874, c876, c878;
    uint32_t p20, p24, p28, p2c, p30, p34, p38, p3c;
    uint32_t scale0, scale1, scale2;
    uint32_t a0[15], a1[15], a2[15];
    uint32_t tail0, tail1, tail2;
    uint32_t q0, q1, v;
    uint16_t h;
    int rc;

    if (!rt || !rt->mmio) return -EINVAL;
    if (!(rt->ctx[0x13] & 0x01u)) return 0;

    mode = rt->ctx[0x870];
    rc = stock_gain_quantity(rt, &gain);
    if (rc) return rc;

    memcpy(&h, rt->ctx + 0x874, sizeof(h)); c874 = h;
    memcpy(&h, rt->ctx + 0x876, sizeof(h)); c876 = h;
    memcpy(&h, rt->ctx + 0x878, sizeof(h)); c878 = h;

    if (mode & 0x04u)
        scalar = stock_d2910(rt->ctx + 0x88c, gain, 1u);
    else
        scalar = rt->ctx[0x87a];

    v = rd(rt, 0x4f8);
    v = (v & 0x0000ffffu) | ((((scalar * c874) >> 6) & 0xffffu) << 16);
    wr(rt, 0x4f8, v);

    v = rd(rt, 0x4fc);
    v = (v & 0xffff0000u) | (((scalar * c876) >> 6) & 0xffffu);
    v = (v & 0x0000ffffu) | ((((scalar * c878) >> 6) & 0xffffu) << 16);
    wr(rt, 0x4fc, v);

    p20 = rt->ctx[0x87c] ? rt->ctx[0x87c] : 1u;
    p34 = p20;
    p38 = p20 << 24;
    p30 = (rt->ctx[0x87d] <= p20) ? (p20 + 1u) : rt->ctx[0x87d];

    if (mode & 0x01u) {
        scale0 = stock_d2910(rt->ctx + 0x904, gain, 4u);
        scale1 = stock_d2910(rt->ctx + 0x91c, gain, 4u);
        scale2 = stock_d2910(rt->ctx + 0x934, gain, 4u);
        p2c = stock_d2910(rt->ctx + 0x898, gain, 1u);
        p24 = stock_d2910(rt->ctx + 0x8a4, gain, 1u);
        p28 = stock_d2910(rt->ctx + 0x8b0, gain, 1u);
        if (p24 > p28) {
            p28 = p24;
            p3c = 0u;
        } else {
            p3c = (p28 - p24) << 8;
        }
    } else {
        uint32_t w884;
        scale0 = ctx_u16(rt, 0x884) & 0x0fffu;
        memcpy(&w884, rt->ctx + 0x884, sizeof(w884));
        scale1 = (w884 >> 12) & 0x0fffu;
        scale2 = ctx_u16(rt, 0x888) & 0x0fffu;
        p2c = rt->ctx[0x87b];
        p24 = rt->ctx[0x880];
        p28 = rt->ctx[0x881] < p24 ? p24 : rt->ctx[0x881];
        p3c = (p28 - p24) << 8;
    }

    stock_d2074_unpack5(rt->ctx + 0x8bc, scale0, a0);
    stock_d2074_unpack5(rt->ctx + 0x8d4, scale1, a1);
    stock_d2074_unpack5(rt->ctx + 0x8ec, scale2, a2);

    tail0 = ((ctx_u16(rt, 0x8d0) & 0x03ffu) * scale0) >> 6;
    tail1 = ((ctx_u16(rt, 0x8e8) & 0x03ffu) * scale1) >> 6;
    tail2 = ((ctx_u16(rt, 0x900) & 0x03ffu) * scale2) >> 6;

    q0 = ((uint32_t)rt->ctx[0x880] << 8) / p34;
    if (q0 > 0x03ffu) q0 = 0x03ffu;
    q1 = p30 > p20 ? p3c / (p30 - p20) : 0u;
    if (q1 > 0x03ffu) q1 = 0x03ffu;

    v = rd(rt, 0x4f4);
    v = (v & ~0x7u) | ((mode & 0x02u) ? 0u : 7u);
    wr(rt, 0x4f4, v);
    wr(rt, 0x514, q0 << 4);
    wr(rt, 0x518, p38 | (p24 << 12) | q1);
    wr(rt, 0x51c, (p28 << 12) | (p30 << 24));

    v = rd(rt, 0x4f8);
    v = (v & ~0x0000ff00u) | ((p2c << 8) & 0x0000ff00u);
    wr(rt, 0x4f8, v);

    stock_d2074_pack16(rt, 0xa0c, a0, tail0);
    stock_d2074_pack16(rt, 0xa2c, a1, tail1);
    stock_d2074_pack16(rt, 0xa4c, a2, tail2);
    return 0;
}

/* CEACC -> ISP +0x32c. */
int fh_isp_runtime_apply_ceacc(struct fh_isp_runtime *rt)
{
    uint32_t v, mode, submode, gain, r5, r6, r7, r8, r0;
    static const uint32_t map_bits[3] = { 0u, 3u, 2u };
    if (!rt || !rt->mmio) return -EINVAL;
    if (!(rt->ctx[0x11] & 0x08u)) return 0;
    if (!(rd(rt, 0x024) & 0x00200000u)) return 0;

    mode = (rt->ctx[0x1b0] >> 4) & 3u;
    v = rd(rt, 0x32c);
    if (mode == 1u) { v |= 0x04u; v &= ~0x08u; }
    else if (mode == 2u) { v &= ~0x04u; v |= 0x08u; }
    else if (mode > 2u) v |= 0x0cu;
    else v &= ~0x0cu;

    r8 = rt->ctx[0x1b0] & 3u;
    r5 = rt->ctx[0x1b4] & 0x0fu;
    r6 = rt->ctx[0x1b6];
    r7 = rt->ctx[0x1b7];
    memcpy(&gain, rt->ctx + 0x60, sizeof(gain));
    gain >>= 12;
    if (r8 == 1u) {
        r5 = stock_d2910(rt->ctx + 0x1b8, gain, 0u);
        r6 = stock_d2910(rt->ctx + 0x1c0, gain, 1u);
        r7 = stock_d2910(rt->ctx + 0x1cc, gain, 1u);
    }
    if (r5 > 7u) r5 = 7u;

    if (rt->ctx[0x1b1] & 1u) {
        r0 = stock_d2910(rt->ctx + 0x1d8, gain, 0u);
        if (r0 > 3u) r0 = 3u;
        submode = r0 - 1u;
        r0 = (submode > 2u) ? 0u : ((map_bits[submode] << 8) & 0x300u);
    } else {
        uint32_t raw = (rt->ctx[0x1b0] >> 2) & 3u;
        if (raw > 3u) raw = 3u;
        submode = raw - 1u;
        r0 = (submode > 2u) ? 0u : ((map_bits[submode] << 8) & 0x300u);
    }

    v = (v & ~0x00000001u) | (r8 & 1u);
    v = (v & ~0x00000030u) | ((r5 & 3u) << 4);
    v = (v & ~0x000000c0u) | ((r5 & 3u) << 6);
    v = (v & ~0x00000300u) | r0;
    v = (v & ~0x00ff0000u) | ((r6 & 0xffu) << 16);
    v = (v & ~0xff000000u) | ((r7 & 0xffu) << 24);
    wr(rt, 0x32c, v);
    return 0;
}


/* CFD70: exact reciprocal-range normalization writer.
 * Active when ctx[0x11] bit2 is set and high endpoint > low endpoint.
 * Stock computes q=(1<<32)/(hi-lo), stores range endpoints in +0x330 and
 * a normalized 17-bit reciprocal mantissa plus exponent in +0x334. */
int fh_isp_runtime_apply_cfd70(struct fh_isp_runtime *rt)
{
    uint16_t lo, hi;
    uint32_t delta, lg, exp6, mant, v330, v334;
    uint64_t q;
    if (!rt || !rt->mmio) return -EINVAL;
    if (!(rt->ctx[0x11] & 0x04u)) return 0;
    memcpy(&lo, rt->ctx + 0x1acu, sizeof(lo));
    memcpy(&hi, rt->ctx + 0x1aeu, sizeof(hi));
    if (hi <= lo) return 0;

    delta = (uint32_t)hi - (uint32_t)lo;
    q = (1ull << 32) / delta;
    if (!q) return 0;
    lg = 0;
    { uint64_t t = q; while (t >>= 1) ++lg; }
    exp6 = (32u - lg) & 0x3fu;
    mant = (uint32_t)(q >> (lg > 16u ? (lg - 16u) : 0u)) & 0x1ffffu;

    v330 = rd(rt, 0x330);
    v330 = (v330 & 0x00000000u) | ((uint32_t)lo << 16) | (uint32_t)hi;
    wr(rt, 0x330, v330);

    v334 = rd(rt, 0x334);
    v334 &= 0xfe0000c0u;
    v334 |= exp6;
    v334 |= (mant << 8) & 0x01ffff00u;
    wr(rt, 0x334, v334);
    return 0;
}


/* CED28: stock parameter-update commit/dirty-consume stage.
 * Vendor code additionally copies a 0x280-byte prepared block into its
 * internal module buffer.  In the replacement runtime those module buffers
 * are represented directly by ctx, so the observable equivalent is to
 * consume the profile dirty flag only after all preceding dirty-dependent
 * writers have run. */
int fh_isp_runtime_apply_ced28(struct fh_isp_runtime *rt)
{
    if (!rt) return -EINVAL;
    ctx_set_u32(rt, 0x11a8u, 0u);
    if (!(rt->ctx[0x367] & 0x80u) && !rt->params_dirty &&
        !rt->gamma_candidate_pending)
        return 0;
    if (!rt->gamma_candidate_pending) {
        ctx_set_u32(rt, 0x11a8u, UINT32_MAX);
        return 0;
    }
    memcpy(rt->ctx + 0x0f20u, rt->ctx + 0x05f0u, 0x280u);
    ctx_set_u32(rt, 0x11a0u, rt->gamma_meta0);
    ctx_set_u32(rt, 0x11a4u, rt->gamma_meta1);
    rt->gamma_candidate_pending = 0;
    rt->ctx[0x367] &= (uint8_t)~0x80u;
    rt->params_dirty = 0;
    ctx_set_u32(rt, 0x11a8u, 1u);
    return 0;
}



/* Shared stock trigonometric/math helpers.  The sine table is exact Apollo
 * data recovered at VA 0x2AC09C (360 x int16_t, Q7-like). */
static int32_t stock_sdiv32(int32_t n, int32_t d)
{
    return d ? n / d : 0;
}

static int stock_d1258_ratio_sat1023(unsigned angle)
{
    int32_t den = fh8626_stock_sin_q7_lut[angle + 90u];
    int32_t q;
    if (den == 0) return 1023;
    q = stock_sdiv32((int32_t)fh8626_stock_sin_q7_lut[angle] << 8, den);
    return (q < 1023) ? q : 1023;
}

/* Literal D1258 guard behavior for the /2../6 branches. */
static int stock_d1258_guarded_ratio(unsigned angle_b, unsigned cos_mod,
                                     unsigned div, int last)
{
    unsigned pre = cos_mod / div;
    unsigned a = angle_b / div;
    int32_t den, q;
    if (fh8626_stock_sin_q7_lut[pre] == 0)
        return 1023;
    den = fh8626_stock_sin_q7_lut[a + 90u];
    if (den == 0)
        return 1023;
    q = stock_sdiv32((int32_t)fh8626_stock_sin_q7_lut[a] << 8, den);
    if (last)
        return (q <= 1022) ? q : 1023;
    return (q < 1023) ? q : 1023;
}

static uint32_t stock_mod360_i32(int32_t v)
{
    int32_t r = v % 360;
    if (r < 0) r += 360;
    return (uint32_t)r;
}

static uint32_t stock_d1db0_hi12(unsigned scalar, int32_t c)
{
    uint32_t p = (uint32_t)(scalar * (uint32_t)c);
    return (p << 10) & 0x0fff0000u;
}

static uint32_t stock_d1db0_lo12(unsigned scalar, int32_t c)
{
    int32_t p = (int32_t)(scalar * (uint32_t)c);
    return ((uint32_t)(p >> 6)) & 0x0fffu;
}

int fh_isp_runtime_set_d1db0_coeffs(struct fh_isp_runtime *rt,
                                    const struct fh_isp_stock_d1db0_coeffs *c)
{
    if (!rt) return -EINVAL;
    if (!c) {
        memset(&rt->d1db0_coeffs, 0, sizeof(rt->d1db0_coeffs));
        rt->d1db0_coeffs_valid = 0;
        return 0;
    }
    rt->d1db0_coeffs = *c;
    rt->d1db0_coeffs_valid = 1;
    return 0;
}

int fh_isp_runtime_set_d16a4_rows(struct fh_isp_runtime *rt,
                                  const uint32_t rows[8][6])
{
    if (!rt) return -EINVAL;
    rt->d16a4_rows = rows;
    return 0;
}

int fh_isp_runtime_set_d1724_stats(struct fh_isp_runtime *rt,
                                   const struct fh_isp_stock_d1724_stat24 stats[32])
{
    if (!rt) return -EINVAL;
    if (!stats) {
        memset(rt->d1724_stats, 0, sizeof(rt->d1724_stats));
        rt->d1724_stats_valid = 0;
        return 0;
    }
    memcpy(rt->d1724_stats, stats, sizeof(rt->d1724_stats));
    rt->d1724_stats_valid = 1;
    return 0;
}

/* D1DB0 exact consumer algorithm. The stock GOT 0x316D14 coefficient object
 * is installed by reset; the setter remains available for controlled IQ
 * overrides. */
int fh_isp_runtime_apply_d1db0_lut(struct fh_isp_runtime *rt)
{
    uint32_t gain, scalar, d9, r;
    uint8_t ctl;
    int32_t signed_src;
    unsigned si, ci, a, b;
    int rc;

    if (!rt || !rt->mmio) return -EINVAL;
    if (!(rt->ctx[0x12] & 0x08u)) return 0;

    ctl = rt->ctx[0x2d8];
    rc = stock_gain_quantity(rt, &gain);
    if (rc) return rc;

    d9 = rt->ctx[0x2d9];
    if (ctl & 0x01u)
        d9 = stock_d2910(rt->ctx + 0x2dc, gain, 1u);

    scalar = rt->ctx[0x2b0];
    if ((rt->ctx[0x2ac] & 0x03u) == 1u)
        scalar = stock_d2910(rt->ctx + 0x2c0, gain, 1u);

    if ((rt->ctx[0x2ac] & 0x0cu) == 0x04u)
        signed_src = (int32_t)stock_d2910(rt->ctx + 0x2cc, gain, 1u);
    else
        signed_src = (int8_t)rt->ctx[0x2b1];

    r = rd(rt, 0x5c8);
    r = (r & 0xfffffc00u) | (scalar & 0x03ffu);
    r = (r & 0xff00ffffu) | (((uint32_t)signed_src & 0xffu) << 16);
    wr(rt, 0x5c8, r);

    si = stock_mod360_i32(((int32_t)(int8_t)ctl) >> 1);
    ci = (si + 90u) % 360u;
    r = rd(rt, 0x5cc);
    r = (r & ~0x000001ffu) | ((uint16_t)fh8626_stock_sin_q7_lut[si] & 0x01ffu);
    r = (r & ~0x01ff0000u) |
        (((uint32_t)(uint16_t)fh8626_stock_sin_q7_lut[ci] & 0x01ffu) << 16);
    wr(rt, 0x5cc, r);

    r = rd(rt, 0x5d0);
    r = (r & 0xfffffc00u) | ((d9 << 2) & 0x03ffu);
    wr(rt, 0x5d0, r);

    if (!rt->d1db0_coeffs_valid)
        return 0;

    a = rt->ctx[0x2da]; if (a > 200u) a = 200u;
    b = rt->ctx[0x2db]; if (b > 200u) b = 200u;
    r = rd(rt, 0x4dc);
    r = (r & 0xf000ffffu) | stock_d1db0_hi12(a, rt->d1db0_coeffs.c16);
    wr(rt, 0x4dc, r);
    r = rd(rt, 0x4e0);
    r = (r & 0xf000ffffu) | stock_d1db0_hi12(a, rt->d1db0_coeffs.c24);
    r = (r & 0xfffff000u) | stock_d1db0_lo12(a, rt->d1db0_coeffs.c20);
    wr(rt, 0x4e0, r);
    r = rd(rt, 0x4e4);
    r = (r & 0xf000ffffu) | stock_d1db0_hi12(b, rt->d1db0_coeffs.c36);
    r = (r & 0xfffff000u) | stock_d1db0_lo12(b, rt->d1db0_coeffs.c32);
    wr(rt, 0x4e4, r);
    r = rd(rt, 0x4e8);
    r = (r & 0xfffff000u) | stock_d1db0_lo12(b, rt->d1db0_coeffs.c40);
    wr(rt, 0x4e8, r);
    return 0;
}

/* D1258 exact ARM-equivalent active path. */
int fh_isp_runtime_apply_d1258(struct fh_isp_runtime *rt)
{
    uint16_t au, bu;
    unsigned a, b, cos_a, cos_b;
    int r1, r2, r3, r4, r5, r6;
    uint32_t v59c;
    if (!rt || !rt->mmio) return -EINVAL;
    if (!(rt->ctx[0x13] & 0x10u)) return 0;

    memcpy(&au, rt->ctx + 0x978, 2);
    memcpy(&bu, rt->ctx + 0x97a, 2);
    a = au & 0x1ffu;
    b = bu & 0x1ffu;
    if (a >= 360u || b >= 360u) return -ERANGE;
    cos_a = (a + 90u) % 360u;
    cos_b = (b + 90u) % 360u;

    r1 = stock_d1258_ratio_sat1023(b);
    r2 = stock_d1258_guarded_ratio(b, cos_b, 2u, 0);
    r3 = stock_d1258_guarded_ratio(b, cos_b, 3u, 0);
    r4 = stock_d1258_guarded_ratio(b, cos_b, 4u, 0);
    r5 = stock_d1258_guarded_ratio(b, cos_b, 5u, 0);
    r6 = stock_d1258_guarded_ratio(b, cos_b, 6u, 1);

    v59c = rd(rt, 0x59c);
    v59c = (v59c & ~0x000000f1u) |
           ((uint32_t)rt->ctx[0x974] & 0xf0u) |
           ((uint32_t)rt->ctx[0x974] & 1u);
    wr(rt, 0x59c, v59c);
    wr(rt, 0x5a0, (uint32_t)(uint8_t)fh8626_stock_sin_q7_lut[a] |
                   ((uint32_t)(uint8_t)fh8626_stock_sin_q7_lut[cos_a] << 8));
    wr(rt, 0x5a4, ((uint32_t)r1 & 0x3ffu) |
                   (((uint32_t)r2 & 0x3ffu) << 10) |
                   (((uint32_t)r3 & 0x3ffu) << 20));
    wr(rt, 0x5a8, ((uint32_t)r4 & 0x3ffu) |
                   (((uint32_t)r5 & 0x3ffu) << 10) |
                   (((uint32_t)r6 & 0x3ffu) << 20));
    { uint32_t v;
      memcpy(&v, rt->ctx + 0x97c, 4); wr(rt, 0x5ac, v);
      memcpy(&v, rt->ctx + 0x980, 4); wr(rt, 0x5b0, v);
      memcpy(&v, rt->ctx + 0x984, 4); wr(rt, 0x5b4, v); }
    return 0;
}

static uint32_t stock_cfe78(unsigned a)
{
    if (a <= 90u) return 0u;
    if (a <= 180u) return 1u;
    if (a <= 269u) return 2u;
    return 3u;
}

static int32_t stock_clamp_s32(int32_t v, int32_t lo, int32_t hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static int32_t stock_mul_s32_wrap(int32_t a, int32_t b)
{
    return (int32_t)(uint32_t)((int64_t)a * (int64_t)b);
}

/* CFEB0 exact ARM arithmetic. */
static int stock_cfeb0(unsigned a, unsigned b, unsigned c, uint32_t out[8])
{
    int32_t sa, s2a, sb, sa2, sb2, c2, delta, inv_sa2, x, den;
    unsigned sum, diff;
    if (!out) return -EINVAL;
    sa = fh8626_stock_sin_q7_lut[a % 360u];
    s2a = fh8626_stock_sin_q7_lut[(a * 2u) % 360u];
    sb = fh8626_stock_sin_q7_lut[b % 360u];
    sa2 = sa * sa; sb2 = sb * sb; c2 = (int32_t)(c * c);
    if (!c2 || !sb2) return -ERANGE;
    delta = (int32_t)((uint32_t)c2 - 4096u);
    inv_sa2 = 16384 - sa2;

    x = stock_mul_s32_wrap(delta, sa2);
    x = stock_sdiv32(x, sb2);
    x = (int32_t)((uint32_t)x + 4096u);
    x = (int32_t)((uint32_t)x << 8);
    x = stock_sdiv32(x, c2);
    out[0] = (uint32_t)stock_clamp_s32(x, -65536, 8191);

    x = (int32_t)(((uint32_t)delta << 24) - (uint32_t)delta);
    x = (int32_t)((uint32_t)x << 8);
    x = stock_sdiv32(x, c2);
    x = stock_mul_s32_wrap((int32_t)((uint32_t)s2a << 7), x);
    x = stock_sdiv32(x, sb2);
    out[1] = (uint32_t)stock_clamp_s32(x, -8192, 8191);

    x = stock_mul_s32_wrap(inv_sa2, delta);
    x = stock_sdiv32(x, sb2);
    x = (int32_t)((uint32_t)x + 4096u);
    x = (int32_t)((uint32_t)x << 8);
    x = stock_sdiv32(x, c2);
    out[2] = (uint32_t)stock_clamp_s32(x, -65536, 8191);

    sum = (a + b) % 360u;
    if (sum == 90u || sum == 270u) x = 16383;
    else {
        den = fh8626_stock_sin_q7_lut[(sum + 90u) % 360u];
        if (!den) return -ERANGE;
        x = stock_sdiv32((int32_t)((uint32_t)fh8626_stock_sin_q7_lut[sum] << 8), den);
    }
    out[3] = (uint32_t)stock_clamp_s32(x, -16384, 16383);

    diff = (a + 360u - b) % 360u;
    if (diff == 90u || diff == 270u) x = 16383;
    else {
        den = fh8626_stock_sin_q7_lut[(diff + 90u) % 360u];
        if (!den) return -ERANGE;
        x = stock_sdiv32((int32_t)((uint32_t)fh8626_stock_sin_q7_lut[diff] << 8), den);
    }
    out[4] = (uint32_t)stock_clamp_s32(x, -16384, 16383);
    out[5] = stock_cfe78(a);
    out[6] = stock_cfe78(sum);
    out[7] = stock_cfe78(diff);
    return 0;
}

/* D0238 exact descriptor decode + CFEB0 + packing. */
int fh_isp_runtime_apply_d0238(struct fh_isp_runtime *rt)
{
    unsigned i;
    if (!rt || !rt->mmio) return -EINVAL;
    for (i = 0; i < 3u; ++i) {
        uint32_t d, o[8] = {0}, A, B, C;
        uint16_t hi;
        unsigned a, b, c, off = 0x5d8u + i * 12u;
        memcpy(&d, rt->ctx + 0x988u + i * 4u, 4);
        if (((d >> 24) & 0x02u) == 0u) {
            wr(rt, off, 0); wr(rt, off + 4, 0); wr(rt, off + 8, 0);
            continue;
        }
        a = d & 0x1ffu; if (a > 359u) a = 359u;
        b = (d >> 9) & 0xffu; if (b < 15u) b = 15u; if (b > 89u) b = 89u;
        hi = (uint16_t)(d >> 16);
        if (((uint16_t)0x01feu & (uint16_t)~hi) == 0u) c = 255u;
        else { c = (hi >> 1) & 0xffu; if (c < 39u) c = 39u; }
        if (stock_cfeb0(a, b, c, o)) return -ERANGE;
        A = (o[0] & 0x0001ffffu) | (o[1] << 18);
        B = (o[2] & 0x0001ffffu) |
            ((o[5] << 20) & 0x00300000u) |
            ((o[6] << 24) & 0x03000000u) |
            ((o[7] << 28) & 0x30000000u);
        C = (o[3] & 0x00007fffu) | ((o[4] << 16) & 0x7fff0000u);
        wr(rt, off, A); wr(rt, off + 4, B); wr(rt, off + 8, C);
    }
    return 0;
}

/* D1724 cluster. Current GC1054 day profile disables ctx[0x12] bit1.
 * The exact GOT 0x3165D4 rows are installed by reset; profiles that enable
 * the adaptive path still require a live C6A44-derived 32x24 statistic set. */
int fh_isp_runtime_apply_d1724(struct fh_isp_runtime *rt)
{
    struct fh8626_d1724_sums sums;
    struct fh8626_d1724_adaptive_debug dbg;
    unsigned i, j;
    if (!rt || !rt->mmio) return -EINVAL;
    if (!(rt->ctx[0x12] & 0x02u)) return 0;
    if (rt->isp_cfg && rt->isp_cfg_size >= 0x208c8u) {
        volatile const uint32_t *src =
            (volatile const uint32_t *)(rt->isp_cfg + 0x205c8u);
        for (i = 0u; i < 32u; ++i) {
            uint32_t *dst = &rt->d1724_stats[i].v0;
            for (j = 0u; j < 6u; ++j) dst[j] = src[i * 6u + j];
        }
        rt->d1724_stats_valid = 1;
    }
    if (!rt->d16a4_rows || !rt->d1724_stats_valid) return -ENODATA;

    fh8626_d16a4_apply_row((uint32_t *)(uintptr_t)rt->mmio, rt->ctx,
                           rt->d16a4_rows);
    fh8626_d1724_sum_stats((const struct fh8626_d1724_stat24 *)rt->d1724_stats,
                           &sums);
    if (rt->ctx[0x0a3c] & 0x02u)
        fh8626_d1724_apply_adaptive((uint32_t *)(uintptr_t)rt->mmio, rt->ctx,
                                    &sums, &dbg);
    else
        fh8626_d1724_apply_default((uint32_t *)(uintptr_t)rt->mmio, rt->ctx,
                                   &sums);
    return 0;
}

/* C9740/C9C74/C9898 control-state core.  Stock C9868 clears exactly 9 u32
 * slots and a separate dirty mask.  C9740 then initializes slot1 through the
 * sensor callback at +0x18 (get_intt), slot2 through +0x0c (get_gain), slot3
 * from the 13-bit ISP +0x168 field, slot4=1 and slots5..8=0. */
int fh_isp_runtime_c949c_set_slot(struct fh_isp_runtime *rt, unsigned index, uint32_t value)
{
    if (!rt || index > 8u) return -EINVAL;
    rt->ae_state.slot[index] = value;
    return 0;
}

int fh_isp_runtime_cb890_init_ae_state(struct fh_isp_runtime *rt)
{
    uint32_t v168, value;
    if (!rt || !rt->mmio) return -EINVAL;
    memset(&rt->ae_state, 0, sizeof(rt->ae_state));
    memset(rt->ae_stats, 0, sizeof(rt->ae_stats));
    if (rt->sensor) {
        value = 0u;
        if (fh_sensor_gc1054_get_intt(rt->sensor, &value) == 0)
            rt->ae_state.slot[1] = value;
        value = 0u;
        if (fh_sensor_gc1054_get_gain(rt->sensor, &value) == 0)
            rt->ae_state.slot[2] = value;
    }
    v168 = rd(rt, 0x168u) & 0x1fffu;
    rt->ae_state.slot[3] = v168;
    rt->ae_state.slot[4] = 1u;
    rt->ae_state.slot[5] = 0u;
    rt->ae_state.slot[6] = 0u;
    rt->ae_state.slot[7] = 0u;
    rt->ae_state.slot[8] = 0u;
    rt->ae_state.dirty_mask = 0u;
    return 0;
}

/* CFD00: CB890 one-time module init.  Besides binding the shared context in
 * vendor globals, its only externally visible semantic is clearing bit20 of
 * ISP +0x024.  The monolithic replacement does not need the vendor global. */
int fh_isp_runtime_cb890_init_cfd00(struct fh_isp_runtime *rt)
{
    uint32_t v;
    if (!rt || !rt->mmio) return -EINVAL;
    v = rd(rt, 0x024u);
    wr(rt, 0x024u, v & ~0x00100000u);
    return 0;
}

/* D0460: one-time geometry-derived module state.  Stock uses the packed
 * geometry from ISP +0x030, multiplies the low/high 11-bit extents and
 * initializes four 0x18-byte records.  Preserve ARM 32-bit shift/truncation
 * semantics for area_q11. */
/* CB6C4: raw channel-order initialization and shared CAFC0 mirror. Current
 * Ghidra contains these 16 table bytes at 0x2b010c, but GOT3169c8 is unmapped:
 * its binding remains retained TARGET_LIVE evidence, not a proven relocation.
 * Preserve the existing mapping; do not claim uniqueness proves that binding.
 * Stock selects three raw halfwords from224/228 by (ISP024>>1)&3, with no
 * inverse-BLC scaling, clamp or reset of CAFC0 dwell/recovery state. */
int fh_isp_runtime_cb890_init_cb6c4(struct fh_isp_runtime *rt, uint32_t owner_arg)
{
    static const uint8_t channel_perm[4][4] = {
        {0u, 1u, 3u, 2u},
        {1u, 0u, 2u, 3u},
        {3u, 2u, 0u, 1u},
        {2u, 3u, 1u, 0u},
    };
    uint32_t v224, v228, mode;
    uint32_t ch[4];
    const uint8_t *p;

    if (!rt || !rt->mmio) return -EINVAL;

    v224 = rd(rt, 0x224u);
    v228 = rd(rt, 0x228u);
    ch[0] = v224 & 0xffffu;
    ch[1] = v224 >> 16;
    ch[2] = v228 & 0xffffu;
    ch[3] = v228 >> 16;
    mode = (rd(rt, 0x024u) >> 1) & 3u;
    p = channel_perm[mode];

    rt->cb6c4_state.owner_arg = owner_arg;
    rt->cb6c4_state.reserved_a4 = 0u;
    rt->cb6c4_state.channel_value[0] = ch[p[0]]; /* vendor +0xc4 */
    rt->cb6c4_state.channel_value[1] = ch[p[1]]; /* vendor +0xc8 */
    rt->cb6c4_state.channel_value[2] = ch[p[2]]; /* vendor +0xcc */
    if (rt->awb_init)
        rt->awb_init(rt->awb_init_opaque,rt->cb6c4_state.channel_value);
    return 0;
}

int fh_isp_runtime_cb890_init_d0460(struct fh_isp_runtime *rt)
{
    uint32_t g, w1, h1, area, area_q11, area_div8_q11;
    unsigned i;
    if (!rt || !rt->mmio) return -EINVAL;

    g = rd(rt, 0x030u);
    w1 = g & 0x07ffu;
    h1 = (g >> 16) & 0x07ffu;
    area = w1 * h1;
    area_div8_q11 = (((area >> 3) << 12) & 0xffffffffu) >> 1;
    area_q11 = ((area << 12) & 0xffffffffu) >> 1;

    memset(&rt->d0460_state, 0, sizeof(rt->d0460_state));
    for (i = 0; i < 4u; ++i) {
        rt->d0460_state.rec[i].base_200 = 0x00000200u;
        rt->d0460_state.rec[i].base_e00 = 0x00000e00u;
        rt->d0460_state.rec[i].base_2700 = 0x00002700u;
        rt->d0460_state.rec[i].base_bc00 = 0x0000bc00u;
        rt->d0460_state.rec[i].area_q11 = (i < 2u) ? area_q11 : area_div8_q11;
        rt->d0460_state.rec[i].reserved_zero = 0u;
    }
    return 0;
}

/* Normal C9898 data movement after ioctl 0x8010690E: stock overwrites slot0
 * with the returned first statistics/control word, then mirrors slots0..6
 * into seven records whose stride is exactly 0x18. Then selected diagnostic
 * events and slot7/8 range validation, in stock order. Text/proc-trace output
 * is replaced by a nullable sink; hardware is not controlled by these logs. */
int fh_isp_runtime_c9898_ingest_stat0(struct fh_isp_runtime *rt, uint32_t stat0)
{
    unsigned i;
    if (!rt) return -EINVAL;
    rt->ae_state.slot[0] = stat0;
    for (i = 0; i < 7u; ++i)
        rt->ae_stats[i].value = rt->ae_state.slot[i];
    rt->last_ae_log_error = 0;
    if (rt->ae_log)
        for (i = 0; i < 7u; ++i)
            if (rt->ae_state.dirty_mask & (1u << i))
                rt->ae_log(rt->ae_log_opaque,i,rt->ae_stats[i].value,0);
    /* C9918/C9938 validate before name-table indexing. Invalid state7
     * returns immediately, so action8 notification must not happen then. */
    for (i = 7u; i < 9u; ++i) {
        int invalid = rt->ae_state.slot[i] > (i == 7u ? 2u : 9u);
        if (rt->ae_log && (invalid || (rt->ae_state.dirty_mask & (1u << i))))
            rt->ae_log(rt->ae_log_opaque,i,rt->ae_state.slot[i],invalid);
        if (invalid) {
            rt->last_ae_log_error = -ERANGE; /* stock 0xA0074003 */
            return rt->last_ae_log_error;
        }
    }
    return 0;
}

/* C73F8 exact reduction/history/publication path. Current Ghidra proves three
 * parallel nine-word bands: input sums, divisors and quotient outputs. A zero
 * divisor publishes zero for that group and makes the stock invalid marker
 * sticky; it does not abort the frame or suppress the following C757C stage.
 * The remainder performs the selector-controlled history shift, sensor
 * get_intt/get_gain callbacks, C5AE8/C5B70 gain reads and packed ctx+0x60
 * publication. */
int fh_isp_runtime_apply_c73f8(struct fh_isp_runtime *rt)
{
    uint32_t selector, exposure, gain, isp_gain, total;
    unsigned i;
    if (!rt || !rt->mmio) return -EINVAL;

    for (i = 0u; i < 9u; ++i) {
        if (rt->ltm_group_count[i] == 0u) {
            rt->c73f8_group_value[i] = 0u;
            rt->c73f8_invalid = 1u;
        } else {
            rt->c73f8_group_value[i] =
                rt->ltm_group_sum[i] / rt->ltm_group_count[i];
        }
    }

    selector = rt->ctx[0x3a] & 7u;
    if (selector != 0u) {
        for (i = 0u; i < selector; ++i) {
            rt->exposure_history[i] = rt->exposure_history[i + 1u];
            rt->gain_history[i] = rt->gain_history[i + 1u];
        }
    }

    exposure = rt->exposure_history[selector];
    gain = rt->gain_history[selector];
    if (rt->sensor) {
        (void)fh_sensor_gc1054_get_intt(rt->sensor, &exposure);
        (void)fh_sensor_gc1054_get_gain(rt->sensor, &gain);
    }
    rt->exposure_history[selector] = exposure;
    rt->gain_history[selector] = gain;

    ctx_set_u16(rt, 0x64u, (uint16_t)gain);
    ctx_set_u16(rt, 0x5au, (uint16_t)rt->exposure_history[0]);
    ctx_set_u16(rt, 0x5eu, (uint16_t)rt->gain_history[0]);
    ctx_set_u16(rt, 0x60u, (uint16_t)((ctx_u16(rt, 0x60u) & 0xf000u) |
                                      (rt->total_gain_low12 & 0x0fffu)));

    isp_gain = (rd(rt, 0x168u) >> 3) & 0x03ffu;
    rt->gain_ex = (rd(rt, 0x168u) >> 1) & 0x0fffu;
    ctx_set_u16(rt, 0x5cu, (uint16_t)isp_gain);
    total = (uint32_t)(((uint64_t)isp_gain * rt->gain_history[0]) >> 6);
    if (total > 0x000fffffu) total = 0x000fffffu;
    {
        uint32_t packed;
        memcpy(&packed, rt->ctx + 0x60u, 4u);
        packed = (packed & 0x00000fffu) | (total << 12);
        memcpy(rt->ctx + 0x60u, &packed, 4u);
    }
    if (rt->source_stats_epoch && rt->source_stats_epoch != rt->control_epoch) {
        rt->control_epoch = rt->source_stats_epoch;
        rt->gain_epoch = rt->source_stats_epoch;
    }
    if (rt->gain_epoch == rt->source_stats_epoch && rt->source_stats_epoch &&
        rt->nr3d_warmup_left && rt->nr3d_warmup_epoch != rt->source_stats_epoch) {
        rt->nr3d_warmup_epoch=rt->source_stats_epoch;
        rt->nr3d_warmup_left--;
        if (!rt->nr3d_warmup_left)
            rt->temporal_generation = rt->profile_generation;
    }
    return 0;
}

/* C77DC exact guarded control-geometry ratio and D27C4 Q8 logarithm. */
int fh_isp_runtime_apply_c77dc(struct fh_isp_runtime *rt)
{
    uint64_t maximum, current, numerator, quotient;
    uint32_t metric, denominator, reduced;
    uint16_t log_q8;
    if (!rt) return -EINVAL;

    maximum = (uint64_t)ctx_u16(rt, 0x36u) * ctx_u16(rt, 0x34u) *
              ctx_u16(rt, 0x38u);
    current = (uint64_t)ctx_u16(rt, 0x5eu) * ctx_u16(rt, 0x5cu) *
              ctx_u16(rt, 0x5au);
    metric = ctx_u16(rt, 0x58u) & 0x0fffu;
    numerator = (uint64_t)((metric * metric) >> 12) * (maximum >> 12);
    denominator = (uint32_t)(current >> 12);
    if (!denominator) denominator = 1u;
    quotient = numerator / denominator;
    if (!quotient) reduced = 1u;
    else if (quotient > UINT32_MAX) reduced = UINT32_MAX;
    else reduced = (uint32_t)quotient;
    log_q8 = (uint16_t)fh_d27c4_log2_q8(reduced);
    ctx_set_u16(rt, 0x68u, log_q8);
    return 0;
}

static int64_t control_signed32(uint32_t x)
{
    return x <= INT32_MAX ? (int64_t)x : (int64_t)x - INT64_C(4294967296);
}

static uint32_t control_square(uint32_t x, int arithmetic)
{
    uint32_t p = x * x;
    return (p >> 12) | ((arithmetic && (p & 0x80000000u)) ? 0xfff00000u : 0u);
}

static uint32_t control_log_target(uint32_t low, uint32_t high,
                                    uint32_t range, uint32_t metric,
                                    uint32_t denominator, int arithmetic)
{
    uint64_t numerator = (uint64_t)range * control_square(metric, 0);
    uint32_t q = (uint32_t)(numerator / denominator);
    uint32_t lo_square = control_square(low, arithmetic);
    uint32_t hi_square = control_square(high, arithmetic);
    /* C7AAC sign-extends its ASR result for the full-product zero test. */
    uint64_t hi_product = arithmetic
        ? (uint64_t)control_signed32(hi_square) * range
        : (uint64_t)hi_square * range;
    uint32_t x = fh_d27c4_log2_q8(q ? q : 1u);
    uint32_t a = fh_d27c4_log2_q8(lo_square ? lo_square : 1u);
    uint32_t b = fh_d27c4_log2_q8(hi_product ? (uint32_t)hi_product : 1u);
    int64_t span, delta;
    if (x < a) return low;
    if (x > b) return high;
    span = control_signed32(b - a);
    if (span < 1) span = 1;
    delta = control_signed32((x - a) * (high - low)) / span;
    return low + (uint32_t)delta;
}

/* C7894 and C7AAC: target publication, with ARM-width multiply/shift
 * semantics. q8_aux is the persistent AE state at module+0x1f8. */
int fh_isp_runtime_apply_control_target(struct fh_isp_runtime *rt,
                                         uint32_t q8_aux, int alternate)
{
    uint32_t target, current, maximum, high, low, denominator, range, value;
    if (!rt) return -EINVAL;
    target = (uint32_t)rt->ctx[0x30] << 4;
    if (!(rt->ctx[0x44] & 1u)) {
        rt->control_target_q12 = target;
        rt->ctx[0x66] = rt->ctx[0x30];
        return 0;
    }
    current = ctx_u16(rt, 0x5a);
    maximum = ctx_u16(rt, 0x38);
    high = (uint32_t)ctx_u16(rt, 0x46) << 4;
    if (high < target) high = target;
    low = (uint32_t)rt->ctx[0x45] << 4;
    if (!alternate && current < maximum) {
        /* SMULL by 0x51eb851f, ASR #5: floor(7*maximum/100). */
        uint32_t pivot = maximum * 7u / 100u;
        uint32_t delta = high - target;
        if (current < pivot) {
            uint32_t shift = 0u, v = pivot;
            uint32_t root, pivot_root, segment;
            while ((v >>= 1) != 0u) ++shift; /* D2748 floor(log2) */
            segment = delta - (delta * (maximum - pivot)) / maximum;
            root = stock_isqrt_u32(current << shift);
            pivot_root = stock_isqrt_u32(pivot << shift);
            value = high - (uint32_t)((uint64_t)root * segment / pivot_root);
        } else {
            value = target + (delta * (maximum - current)) / maximum;
        }
    } else {
        range = (ctx_u16(rt, 0x36) >> 6) * (ctx_u16(rt, 0x34) >> 6);
        if (alternate && current < maximum) {
            denominator = (current * q8_aux) >> 8;
            range = maximum;
            low = target;
        } else {
            high = target;
            denominator = (!alternate && (int8_t)rt->ctx[0x2f] >= 0)
                ? ctx_u32(rt, 0x60) >> 18
                : (q8_aux * ctx_u16(rt, 0x5e)) >> 14;
            if (alternate && ctx_u16(rt, 0x42)) {
                maximum *= (uint32_t)rt->ctx[0x3f] + 1u;
                denominator *= current;
                range *= maximum;
            }
        }
        if (!denominator) return 0; /* stock leaves target and ctx+0x66 */
        value = control_log_target(low, high, range, rt->c757c_metric_q12,
                                   denominator, alternate);
    }
    rt->control_target_q12 = value;
    rt->ctx[0x66] = (uint8_t)(value >> 4);
    return 0;
}

/* C949C measurement phase only. Control slots are published AFTER AE, not
 * from these narrowed pre-actuation context fields. */
int fh_isp_runtime_c949c_update_known(struct fh_isp_runtime *rt)
{
    if (!rt || !rt->mmio) return -EINVAL;
    if (!(rt->ctx[0x10] & 0x01u)) return 0;
    return fh_isp_runtime_apply_c73f8(rt);
}

/* C94E0 tail, also reached with AE disabled or invalid populations.
 * Failed providers retain prior valid values, rather than publishing stock's
 * potentially uninitialized stack locals. No context narrowing is applied.
 * C757C module+7C/+80 are full metric/target; C7C3C publishes gate at +204. */
int fh_isp_runtime_c949c_publish_tail(struct fh_isp_runtime *rt,
                                     uint32_t driver_mask, uint32_t gate_state)
{
    uint32_t value;
    if (!rt || !rt->mmio) return -EINVAL;
    rt->ae_state.dirty_mask = driver_mask;
    if ((driver_mask & 2u) && rt->sensor &&
        fh_sensor_gc1054_get_intt(rt->sensor, &value) == 0)
        rt->ae_state.slot[1] = value;
    if ((driver_mask & 4u) && rt->sensor &&
        fh_sensor_gc1054_get_gain(rt->sensor, &value) == 0)
        rt->ae_state.slot[2] = value;
    if (ctx_u32(rt, 0xa70u))
        rt->ae_state.slot[3] = (rd(rt, 0x168u) >> 3) & 0x03ffu;
    rt->ae_state.slot[5] = rt->c757c_metric_q12;
    rt->ae_state.slot[6] = rt->control_target_q12;
    rt->ae_state.slot[7] = gate_state;
    return 0;
}


/* D0DF4 exact top-level control bit. Stock enables this stage with
 * ctx[0x13] bit5.  ctx[0x260] bit1 controls ISP+0x024 bit18 inversely:
 * clear ctx bit -> set MMIO bit18, set ctx bit -> clear MMIO bit18.
 * D0528/D0630 are implemented by fh_isp_runtime_apply_dynamic_ltm(); D0B2C
 * curve-row publication remains a separate dependency. */
static int ltm_tile_log_range(struct fh_isp_runtime *rt,
                              uint32_t *min_log, uint32_t *max_log);

int fh_isp_runtime_get_ltm_attr(const struct fh_isp_runtime *rt,struct fh_isp_ltm_attr *a)
{
    const uint8_t *c;
    unsigned i;
    if (!a) return -3002; /* 216B48 = FFFFF446 */
    if (!rt) return -EINVAL;
    c=rt->ctx;
    a->hw_enable=(c[0x260]>>1)&1u;a->ctrl_mode=(c[0x260]>>2)&3u;
    a->sat_coeff_num=c[0x260]>>4;a->y_coeff_num=c[0x261]&31u;
    a->mode=c[0x260]&1u;a->value_0b=c[0x262];a->value_0c=c[0x263];
    a->value_0d=c[0x264];a->k2_offset=c[0x265]&15u;a->value_0f=c[0x266];
    a->value_10=c[0x268];a->value_11=c[0x269];a->value_12=c[0x26a];
    for(i=0;i<12;i++)a->sat_coeff_map[i]=(c[0x29c+i/2]>>((i&1u)*4))&15u;
    memcpy(a->table_a,c+0x26c,12);memcpy(a->table_b,c+0x278,12);
    memcpy(a->table_c,c+0x284,12);memcpy(a->table_d,c+0x290,12);
    return 0;
}

int fh_isp_runtime_set_ltm_attr(struct fh_isp_runtime *rt,struct fh_isp_ltm_attr *a)
{
    uint8_t *c;
    unsigned i;
    if (!a) return -3002;
    if (!rt) return -EINVAL;
    /* Stock validates in this order and writes clipped values back. Trace
     * printing is omitted; no profile dirty flag or hardware write invented. */
    if(a->hw_enable>1)a->hw_enable=1;
    if(a->ctrl_mode>3)a->ctrl_mode=3;
    if(a->sat_coeff_num>15)a->sat_coeff_num=15;
    if(a->y_coeff_num>31)a->y_coeff_num=31;
    if(a->mode>1)a->mode=1;
    if(a->k2_offset>15)a->k2_offset=15;
    for(i=0;i<12;i++)if(a->sat_coeff_map[i]>15)a->sat_coeff_map[i]=15;
    c=rt->ctx;
    c[0x260]=(uint8_t)(a->mode|(a->hw_enable<<1)|(a->ctrl_mode<<2)|(a->sat_coeff_num<<4));
    c[0x261]=(c[0x261]&0xe0u)|a->y_coeff_num;
    c[0x262]=a->value_0b;c[0x263]=a->value_0c;c[0x264]=a->value_0d;
    c[0x265]=(c[0x265]&0xf0u)|a->k2_offset;c[0x266]=a->value_0f;
    c[0x268]=a->value_10;c[0x269]=a->value_11;c[0x26a]=a->value_12;
    for(i=0;i<6;i++)c[0x29c+i]=a->sat_coeff_map[i*2]|(a->sat_coeff_map[i*2+1]<<4);
    memcpy(c+0x26c,a->table_a,12);memcpy(c+0x278,a->table_b,12);
    memcpy(c+0x284,a->table_c,12);memcpy(c+0x290,a->table_d,12);
    return 0;
}

static void ltm_publish_enable(struct fh_isp_runtime *rt)
{
    uint32_t v=rd(rt,0x024u);
    if(rt->ctx[0x260]&2u)v&=~0x40000u;
    else v|=0x40000u;
    wr(rt,0x024u,v);
}

int fh_isp_runtime_set_ltm_enabled(struct fh_isp_runtime *rt,int enabled)
{
    struct fh_isp_ltm_attr a;
    int rc;
    if(!rt||!rt->mmio)return -EINVAL;
    rc=fh_isp_runtime_get_ltm_attr(rt,&a);
    if(rc)return rc;
    if(a.hw_enable!=(uint32_t)!!enabled){
        a.hw_enable=(uint32_t)!!enabled;
        rc=fh_isp_runtime_set_ltm_attr(rt,&a);
        if(rc)return rc;
    }
    /* Owner's explicit immediate commit; stock public Set itself is deferred.
     * Do not change periodic gate, rerun stale-stat LTM or reset histories. */
    ltm_publish_enable(rt);
    return 0;
}

int fh_isp_runtime_apply_d0df4_control(struct fh_isp_runtime *rt)
{
    uint32_t min_log, max_log;
    if (!rt || !rt->mmio) return -EINVAL;
    if (!(rt->ctx[0x13] & 0x20u)) return 0;
    ltm_publish_enable(rt);
    /* D0DF4 calls D0528 here; D0630 calls it once more internally.  Preserve
       both call points and D0528's module-visible summary publication. */
    (void)ltm_tile_log_range(rt, &min_log, &max_log);
    return 0;
}

static uint32_t ltm_log2_q8(uint32_t x)
{
    uint32_t t = x >> 1, r1 = 0, r2;
    if (!t) return (x - 1u) << 8;
    do { t >>= 1; r2 = r1 + 1u; if (t) r1 = r2; } while (t);
    if (r2 <= 7u)
        return (r2 << 8) + ((x - (1u << r2)) << (8u - r2));
    return (r2 << 8) + ((x - (1u << r2)) >> (r1 - 7u));
}

static uint32_t ltm_poly_bits(uint32_t x, uint8_t bias)
{
    double d = (double)x;
    double p = 2.4e-7 * d * d * d + 5.9e-6 * d * d - 0.00073 * d +
               0.024 + (double)(bias & 0x0fu) / 10.0;
    uint32_t y;
    if (!(p > 0.0)) return 0u;
    /* Stock276CE0 saturates positive out-of-range conversion to UINT32_MAX,
     * then D0630 clamps to1023. Clamp at the exact rounding threshold first
     * so the C conversion is defined even for large statistics-derived x. */
    if (p >= 1022.5 / 1024.0) return 0x0003ff00u;
    y = (uint32_t)(p * 1024.0 + 0.5);
    if (y > 1023u) y = 1023u;
    return (y << 8) & 0x0003ff00u;
}

static int ltm_tile_log_range(struct fh_isp_runtime *rt,
                              uint32_t *min_log, uint32_t *max_log)
{
    volatile const uint32_t *s;
    uint32_t grid, a, b, count, lo = 0xfffu, hi = 0u, i;
    if (!rt->isp_cfg || rt->isp_cfg_size < 0x75c0u) return -EAGAIN;
    s = (volatile const uint32_t *)(rt->isp_cfg + 0x5c8u);
    grid = rd(rt, 0x1ccu);
    a = (grid >> 16) & 0x1fu;
    b = (grid >> 24) & 0x1fu;
    count = a * b + a + b;
    for (i = 0; i < count; ++i) {
        uint32_t den = s[i * 4u + 1u];
        uint32_t sum = s[i * 4u] + s[0x400u + i * 4u] +
                       s[0x800u + i * 4u] + s[0xc00u + i * 4u];
        uint32_t v;
        if (!den) den = 1u;
        v = ((sum / den) >> 2) << 4;
        if (v < lo) lo = v;
        if (v > hi) hi = v;
    }
    if (!lo) lo = 1u;
    if (!hi) hi = 1u;
    *min_log = ltm_log2_q8(lo);
    *max_log = ltm_log2_q8(hi);
    rt->ltm_summary_min_q8 = *min_log;
    rt->ltm_summary_max_q8 = *max_log;
    /* D0600/D0604 publish into module record2 on EACH D0528 call,
     * including the outer D0DF4 call before D0630 starts. */
    rt->d0460_state.rec[2].base_200 = *min_log;
    rt->d0460_state.rec[2].base_e00 = *max_log;
    rt->ltm_summary_calls++;
    return 0;
}

/* D0528+D0630 live dynamic-LTM core. C6934 is isp_cfg+0x5C8 and C6A9C is
 * isp_cfg+0x21250. The stage is inert until that allocation is explicitly
 * attached, and rejects degenerate geometry before any register publication. */
int fh_isp_runtime_apply_dynamic_ltm(struct fh_isp_runtime *rt)
{
    struct fh_isp_stock_d0460_record *h;
    struct fh_isp_stock_d0460_record previous1, previous2;
    volatile const uint32_t *s;
    uint32_t min_log, max_log, packed, pixels, source0, source1, source2;
    uint32_t old0, old1, old2, old3, old4, old5, n0, target, q, k, low, upper;
    uint32_t old10, q1, q2, x, v;
    uint8_t poly_bias;
    int32_t d;
    uint64_t pair;
    int rc;
    if (!rt || !rt->mmio) return -EINVAL;
    if (!(rt->ctx[0x13] & 0x20u)) return 0;
    if (!rt->isp_cfg || rt->isp_cfg_size < 0x2125cu) return 0;
    packed = rd(rt, 0x030u);
    pixels = (packed & 0x7ffu) * ((packed >> 16) & 0x7ffu);
    if (pixels < 64u) return -ERANGE;
    s = (volatile const uint32_t *)(rt->isp_cfg + 0x21250u);
    /* D0658/D0668/D066C snapshot these BEFORE the internal D0528 scan.
     * Statistics memory is volatile; moving source1 past that scan can
     * combine a different DMA update with the stock control sequence. */
    poly_bias = rt->ctx[0x265];
    source1 = s[1];
    old10 = rd(rt, 0x24cu) & 0x3ffu;
    rc = ltm_tile_log_range(rt, &min_log, &max_log);
    if (rc) return rc;
    packed = rd(rt, 0x030u); /* D06AC: geometry used after the summary */
    pixels = (packed & 0x7ffu) * ((packed >> 16) & 0x7ffu);
    if (pixels < 64u) return -ERANGE;
    h = rt->d0460_state.rec;

    /* D0528 publishes the newest log extrema into the record shifted by
     * D0630. Preserve the complete four-record temporal state. */
    previous1 = h[1];
    previous2 = h[2];
    h[0] = previous1;
    h[1] = previous2;
    old0 = h[3].base_200; old1 = h[3].base_e00;
    old2 = h[3].base_2700; old3 = h[3].base_bc00;
    old4 = h[3].area_q11; old5 = h[3].reserved_zero;
    target = (uint32_t)rt->ctx[0x66] * rt->ctx[0x66] >> 4;
    if (!target) target = 1u;
    target = ltm_log2_q8(target << 4);
    /* D073C source0 follows target/log and history shifting; D0748 source2
     * follows its normalization divide. Do not batch these DMA reads with
     * the deliberately earlier source1 snapshot. */
    source0 = s[0];
    n0 = source0 / (pixels >> 6);
    source2 = s[2];
    d = (int32_t)(((previous1.base_200 + previous2.base_200) >> 1) - old0);
    h[3].base_200 = old0 + (uint32_t)(d / 8);
    d = (int32_t)(((previous1.base_e00 + previous2.base_e00) >> 1) - old1);
    h[3].base_e00 = old1 + (uint32_t)(d / 3);
    d = (int32_t)(n0 - old2);
    h[3].base_2700 = old2 + (uint32_t)(d / 8);
    if (h[3].base_2700 < 0x1100u) h[3].base_2700 = 0x1100u;
    if (h[3].base_2700 > 0xffffu) h[3].base_2700 = 0xffffu;
    d = (int32_t)(source2 - old3);
    h[3].base_bc00 = old3 + (uint32_t)(d / 8);
    if (h[3].base_bc00 < 0xa100u) h[3].base_bc00 = 0xa100u;
    if (h[3].base_bc00 > 0xffffu) h[3].base_bc00 = 0xffffu;
    pair = ((uint64_t)old5 << 32) | old4;
    q = (uint32_t)((pair << 3) / pixels);
    q += (uint32_t)((int32_t)(target - q) / 3);
    h[3].area_q11 = (q >> 3) * pixels;
    h[3].reserved_zero = 0u;

    k = (q * rt->ctx[0x269]) >> 6;
    if (k < 0x300u) k = 0x300u;
    if (k > 0xe00u) k = 0xe00u;
    low = (h[3].base_200 * (rt->ctx[0x268] ? rt->ctx[0x268] : 1u)) >> 6;
    if (low < 0x200u) low = 0x200u;
    if (low > k - 0x100u) low = k - 0x100u;
    upper = (h[3].base_e00 * rt->ctx[0x26a]) >> 6;
    if (upper > 0xfffu) upper = 0xfffu;
    q1 = (old10 << 12) / ((k > low) ? k - low : 1u);
    q2 = ((1023u - old10) << 12) /
         ((upper > k && upper - k > 0x100u) ? upper - k : 0x100u);

    v = rd(rt, 0x240u); wr(rt, 0x240u, (v & 0xffff0000u) | h[3].base_2700);
    v = rd(rt, 0x244u); wr(rt, 0x244u, (v & 0xffff0000u) | h[3].base_bc00);
    /* D0A74/D0A8C are TWO ordered masked writes, not one merged store. */
    v = rd(rt, 0x248u); wr(rt, 0x248u, (v & 0xfffff000u) | low);
    v = rd(rt, 0x248u); wr(rt, 0x248u, (v & 0xf000ffffu) | (k << 16));
    v = rd(rt, 0x24cu); wr(rt, 0x24cu, (v & 0x3ffu) | ((q1 & 0x3fffffu) << 10));
    v = rd(rt, 0x250u); wr(rt, 0x250u, (v & 0xffc00000u) | (q2 & 0x3fffffu));
    x = ((source1 / (pixels >> 2)) + 8u) >> 4;
    v = rd(rt, 0x23cu); wr(rt, 0x23cu, (v & 0xfffc00ffu) | ltm_poly_bits(x, poly_bias));
    return 0;
}

/* Complete D0B2C profile-independent curve publisher. Both source banks and
 * all selector/mode semantics are extracted from the current Ghidra program. */
int fh_isp_runtime_apply_d0b2c(struct fh_isp_runtime *rt)
{
    uint32_t gain, row_a, row_b, ctl_a, ctl_b, ctl_c, mode, v;
    uint32_t field_low, field_middle, field_high;
    const uint16_t *curve_a, *curve_b;
    unsigned i;
    int rc;
    if (!rt || !rt->mmio) return -EINVAL;
    if (!(rt->ctx[0x13] & 0x20u)) return 0;
    mode = (rt->ctx[0x260] >> 2) & 3u;
    if (mode > 2u) mode = 2u;
    if (rt->ctx[0x260] & 1u) {
        rc = stock_gain_quantity(rt, &gain);
        if (rc) return rc;
        row_a = stock_d2910(rt->ctx + 0x290u, gain, 1u) & 0xffu;
        row_b = stock_d2910(rt->ctx + 0x29cu, gain, 0u) & 0xffu;
        ctl_b = stock_d2910(rt->ctx + 0x284u, gain, 1u);
        ctl_a = stock_d2910(rt->ctx + 0x278u, gain, 1u);
        ctl_c = stock_d2910(rt->ctx + 0x26cu, gain, 1u);
    } else {
        row_a = rt->ctx[0x261] & 0x1fu;
        row_b = rt->ctx[0x260] >> 4;
        ctl_a = rt->ctx[0x262];
        ctl_b = rt->ctx[0x263];
        ctl_c = rt->ctx[0x266];
    }
    if (row_a > 24u) row_a = 24u;
    if (row_b > 9u) row_b = 9u;
    curve_a = fh_d0b2c_be4[row_a];
    curve_b = fh_d0b2c_948[row_b];

    if (mode == 0u) {
        if (ctl_b < ctl_a) return 0;
        field_low = (ctl_a << 4) & 0xff0u;
        field_middle = (ctl_b << 16) & 0x00fff000u;
        field_high = (ctl_b - ctl_a) << 25;
    } else if (mode == 1u) {
        field_low = 0xfffu;
        field_middle = 0x00fff000u;
        field_high = 0u; /* D0BAC: clear stale mode0 high byte */
    } else {
        field_low = field_middle = 0u;
        field_high = 0xff000000u;
    }
    /* D0BF4/D0C0C/D0C20: preserve all three ordered volatile RMWs. */
    v = rd(rt, 0x254u); wr(rt, 0x254u, (v & 0xfffff000u) | field_low);
    v = rd(rt, 0x254u); wr(rt, 0x254u, (v & 0xff000fffu) | field_middle);
    v = rd(rt, 0x254u); wr(rt, 0x254u, (v & 0x00ffffffu) | field_high);
    v = rd(rt, 0x23cu);
    wr(rt, 0x23cu, (v & 0xffffff00u) | rt->ctx[0x264]);
    v = rd(rt, 0x258u);
    wr(rt, 0x258u, (v & 0xf00fffffu) | ((ctl_c & 0xffu) << 20));

    for (i = 0; i < 36u; i += 2u) {
        wr(rt, 0x270u + (i / 2u) * 4u,
           (uint32_t)curve_a[i] | ((uint32_t)curve_a[i + 1u] << 16));
        wr(rt, 0x2c4u + (i / 2u) * 4u,
           (uint32_t)curve_b[i] | ((uint32_t)curve_b[i + 1u] << 16));
    }
    wr(rt, 0x2b8u, (uint32_t)curve_a[36] | ((uint32_t)curve_a[37] << 16));
    wr(rt, 0x2bcu, (uint32_t)curve_a[38] | ((uint32_t)curve_a[39] << 16));
    wr(rt, 0x2c0u, curve_a[40]);
    wr(rt, 0x30cu, curve_b[36]);
    return 0;
}

static uint32_t pack_u8x4(const uint16_t *p)
{
    return ((uint32_t)p[0] & 0xffu) | (((uint32_t)p[1] & 0xffu) << 8) |
           (((uint32_t)p[2] & 0xffu) << 16) | (((uint32_t)p[3] & 0xffu) << 24);
}

static void cdd6c_apply_word_row(struct fh_isp_runtime *rt, unsigned base,
                                 const uint16_t row[13])
{
    uint32_t v = rd(rt, base);
    wr(rt, base, (v & 0xc00fffffu) | (((uint32_t)row[0] << 20) & 0x3ff00000u));
    wr(rt, base + 4u, pack_u8x4(row + 1));
    wr(rt, base + 8u, pack_u8x4(row + 5));
    wr(rt, base + 12u, pack_u8x4(row + 9));
}

static void cdd6c_apply_byte_row(struct fh_isp_runtime *rt, unsigned base,
                                 const uint16_t row[13])
{
    uint32_t v = rd(rt, base);
    v = (v & 0xfff0e0c0u) | ((uint32_t)row[10] & 0x3fu) |
        (((uint32_t)row[11] & 0x1fu) << 8) |
        (((uint32_t)row[12] & 0x0fu) << 16);
    wr(rt, base, v);
    wr(rt, base + 16u, pack_u8x4(row));
    wr(rt, base + 20u, pack_u8x4(row + 4));
    v = rd(rt, base + 24u);
    wr(rt, base + 24u, (v & 0xffff8080u) |
       ((uint32_t)row[8] & 0x7fu) | (((uint32_t)row[9] & 0x7fu) << 8));
}

/* Complete CDD6C APC direct/gain-mapped controller for every SREG profile. */
int fh_isp_runtime_apply_cdd6c(struct fh_isp_runtime *rt)
{
    uint32_t gain, flags, s0, s1, s2, s3, byte_a, byte_b, word_a, word_b;
    uint32_t p0, p1, v, sq0, sq1, denominator, reciprocal;
    uint16_t h0, h1, h2, h3;
    int rc;
    if (!rt || !rt->mmio) return -EINVAL;
    if (!(rt->ctx[0x12] & 0x20u)) return 0;
    rc = stock_gain_quantity(rt, &gain);
    if (rc) return rc;
    flags = rt->ctx[0x2e8];
    if ((flags & 3u) == 1u) {
        s3 = stock_d2910(rt->ctx + 0x2fcu, gain, 1u);
        s2 = stock_d2910(rt->ctx + 0x308u, gain, 1u);
        s1 = stock_d2910(rt->ctx + 0x314u, gain, 1u);
        s0 = stock_d2910(rt->ctx + 0x320u, gain, 1u);
        p0 = stock_d29c0(rt->ctx + 0x32cu, gain, 1u);
        p1 = stock_d29c0(rt->ctx + 0x338u, gain, 1u);
    } else {
        s3 = rt->ctx[0x2ef]; s2 = rt->ctx[0x2ee];
        s1 = rt->ctx[0x2ec]; s0 = rt->ctx[0x2ed];
        p0 = rt->ctx[0x2fa]; p1 = rt->ctx[0x2fb];
    }
    byte_a = rt->ctx[0x2e9] & 7u;
    byte_b = (rt->ctx[0x2e9] >> 4) & 7u;
    word_a = rt->ctx[0x2ea] & 7u;
    word_b = (rt->ctx[0x2ea] >> 4) & 7u;
    if (flags & 4u) {
        byte_a = stock_d2910(rt->ctx + 0x344u, gain, 0u) & 0xffu;
        byte_b = stock_d2910(rt->ctx + 0x34cu, gain, 0u) & 0xffu;
    }
    if (flags & 8u) {
        word_a = stock_d2910(rt->ctx + 0x354u, gain, 0u) & 0xffu;
        word_b = stock_d2910(rt->ctx + 0x35cu, gain, 0u) & 0xffu;
    }
    if (byte_a > 7u || byte_b > 7u || word_a > 7u || word_b > 7u)
        return -ERANGE;
    memcpy(&h0, rt->ctx + 0x2f0u, 2u); memcpy(&h1, rt->ctx + 0x2f2u, 2u);
    memcpy(&h2, rt->ctx + 0x2f4u, 2u); memcpy(&h3, rt->ctx + 0x2f6u, 2u);

    if (!(flags & 0x40u)) {
        cdd6c_apply_word_row(rt, 0x52cu, fh_cdd6c_word_b[word_a]);
        cdd6c_apply_word_row(rt, 0x54cu, fh_cdd6c_word_a[word_b]);
    }
    if (!(flags & 0x20u)) {
        cdd6c_apply_byte_row(rt, 0x52cu, fh_cdd6c_byte_a[byte_a]);
        cdd6c_apply_byte_row(rt, 0x54cu, fh_cdd6c_byte_b[byte_b]);
    }
    wr(rt, 0x528u, ((flags & 0x10u) << 12) | (s1 & 0xffu) | ((s0 & 0xffu) << 8));
    v = rd(rt, 0x544u);
    wr(rt, 0x544u, (v & 0x0000ffffu) | ((s2 & 0xffu) << 16) | ((s2 & 0xffu) << 24));
    wr(rt, 0x548u, ((uint32_t)h0 & 0x3ffu) | (((uint32_t)h1 & 0x3ffu) << 16));
    v = rd(rt, 0x564u);
    wr(rt, 0x564u, (v & 0x0000ffffu) | ((s3 & 0xffu) << 16) | ((s3 & 0xffu) << 24));
    wr(rt, 0x568u, ((uint32_t)h2 & 0x3ffu) | (((uint32_t)h3 & 0x3ffu) << 16));
    sq0 = (p1 >> 1) * (p1 >> 1);
    sq1 = (p0 >> 1) * (p0 >> 1);
    v = rd(rt, 0x56cu);
    v = (v & 0xc000c000u) | (sq0 & 0x3fffu) | ((sq1 & 0x3fffu) << 16);
    wr(rt, 0x56cu, v);
    denominator = sq0 * 4u > sq1 * 4u ? sq0 * 4u - sq1 * 4u : 1u;
    reciprocal = 0x10000u / denominator;
    v = rd(rt, 0x570u); wr(rt, 0x570u, (v & 0xffff0000u) | (reciprocal & 0xffffu));
    v = rd(rt, 0x574u);
    v = (v & 0xf0ffc0feu) | 0x06000000u | (1u - (rt->ctx[0x2f8] & 1u)) |
        ((0x3fu - (rt->ctx[0x2f9] & 0x3fu)) << 8);
    wr(rt, 0x574u, v);
    return 0;
}

/* D0FEC/NR3D. Current Ghidra proves the signed threshold uses c0*gain*gain,
 * not the linear c0*gain expression present in the quarantined diagnostic
 * snapshot. The outer stock dispatcher additionally requires mode +0x11ac=1. */
int fh_isp_runtime_apply_d0fec(struct fh_isp_runtime *rt)
{
    static const uint32_t preset[4][7] = {
        {0x7a5d2303u,0x00041402u,0x46302a1eu,0x01f00050u,0x0ea00420u,0x2a221c16u,0x46403a32u},
        {0x2e4c2b0bu,0x00041403u,0x5a40301cu,0x04a00140u,0x0e000960u,0x2a221c16u,0x46403a32u},
        {0x263c201bu,0x00041404u,0x5a40301cu,0x05800300u,0x0d200940u,0x2a221c16u,0x46403a32u},
        {0x11402220u,0x00041406u,0x5a40301cu,0x06200380u,0x0be00a20u,0x2a221c16u,0x46403a32u}
    };
    uint32_t gain, ctl, sel, v, dyn0, dyn1, dyn2, mode;
    int16_t c0, c1;
    int64_t threshold;
    int64_t q;
    int32_t low8;
    int rc;
    if (!rt || !rt->mmio) return -EINVAL;
    if (!(rt->ctx[0x11] & 0x40u)) return 0;
    memcpy(&mode, rt->ctx + 0x11ac, sizeof(mode));
    if (mode != 1u) return 0;
    rc = stock_gain_quantity(rt, &gain);
    if (rc) return rc;
    ctl = rt->ctx[0x1ec];
    memcpy(&c0, rt->ctx + 0x1f8, sizeof(c0));
    memcpy(&c1, rt->ctx + 0x1fa, sizeof(c1));
    if (ctl & 1u) {
        dyn0 = stock_d29c0(rt->ctx + 0x204, gain, 1u);
        dyn1 = stock_d2910(rt->ctx + 0x210, gain, 1u) & 0xffu;
        dyn2 = stock_d2910(rt->ctx + 0x21c, gain, 1u);
    } else {
        dyn0 = rt->ctx[0x1f0];
        dyn1 = rt->ctx[0x1f1];
        dyn2 = rt->ctx[0x200];
        if (dyn1 < 7u) dyn1 = 7u;
    }
    threshold = (int64_t)c0 * (int64_t)gain * (int64_t)gain + 0x80000;
    /* D1094..D10B0 retain the high word through both shifts and c1/add8.
     * C5DB8 bounds gain to20 bits, so the signed64 product cannot overflow.
     * Narrowing before the final saturation inverted large valid inputs. */
    q = (threshold >> 20) + (int64_t)c1;
    q = (q + 8) >> 4;
    if (q < -0x80000) q = -0x80000;
    if (q > 0x7ffff) q = 0x7ffff;
    v = rd(rt, 0x44cu);
    wr(rt, 0x44cu, (v & 0xfffff00fu) | ((dyn2 << 4) & 0xff0u));
    low8 = ((int32_t)gain * (int32_t)rt->ctx[0x1f4] + 0x200) >> 10;
    low8 += (int32_t)rt->ctx[0x1f5];
    low8 = (low8 * (int32_t)dyn0 + 8) >> 4;
    if (low8 < 0) low8 = 0;
    if (low8 > 0xff) low8 = 0xff;
    wr(rt, 0x450u, ((uint32_t)(rt->ctx[0x1fc] & 0x0fu) << 28) |
       (((uint32_t)q & 0xfffffu) << 8) | (uint32_t)low8);
    v = rd(rt, 0x46cu);
    wr(rt, 0x46cu, (v & 0xffff00ffu) | ((dyn1 & 0xffu) << 8));
    if (ctl & 4u) return 0;
    sel = (ctl >> 4) & 3u;
    wr(rt, 0x468u, preset[sel][0]);
    v = rd(rt, 0x46cu);
    wr(rt, 0x46cu, (v & 0xfff0ff00u) | (preset[sel][1] & 0x000f00ffu));
    wr(rt, 0x470u, preset[sel][2]);
    wr(rt, 0x474u, preset[sel][3]);
    wr(rt, 0x478u, preset[sel][4]);
    wr(rt, 0x47cu, preset[sel][5]);
    wr(rt, 0x480u, preset[sel][6]);
    return 0;
}

static int tick_control_stage(struct fh_isp_runtime *rt,
                               fh_isp_runtime_ae_hook_fn ae_hook,void *ae_opaque)
{
    int rc;
    /* Stop dependent AE consumers on rejected input, not the whole CB970.
     * This is bounded owner safety; stock faults are not reproduced. */
    if ((rt->ctx[0x10] & 0x01u) && (rt->ctx[0x2c] & 0x10u)) {
        /* C9240 bypasses normal measurement/history/AE and its status tail.
         * Owner dispatches the same control hook to the special family. */
        return ae_hook ? ae_hook(ae_opaque) : -ENOSYS;
    } else if (rt->ctx[0x10] & 0x01u) {
        rc = fh_isp_runtime_snapshot_c6c00(rt); /* C6C00 exact LTM group snapshot */
        if (rc) return rc;
        rc = fh_isp_runtime_c949c_update_known(rt); /* C73F8 precedes C757C in C949C */
        if (rc) return rc;
        /* C73F8 and C949C share 0x31a294. Invalid populations suppress only
         * this epoch's AE branch; C949C clears the latch before its tail. */
        if (rt->c73f8_invalid) {
            rt->c73f8_invalid = 0u;
        } else {
            rc = fh_isp_runtime_apply_c757c_metric(rt, rt->c73f8_group_value);
            if (rc) return rc;
            rc = fh_isp_runtime_apply_control_target(rt, rt->control_q8_aux, 0);
            if (rc) return rc;
            if ((ctx_u32(rt, 0x2c) & 0x80000008u) == 0x80000008u) {
                rc = fh_isp_runtime_apply_control_target(rt, rt->control_q8_aux, 1);
                if (rc) return rc;
            }
            rc = fh_isp_runtime_apply_c77dc(rt);
            if (rc) return rc;
            if (ae_hook) {
                /* C949C continues here with C6D04 -> C90D4 -> AE actuation. */
                /* C949C does not branch on AE/callback return codes.
                 * Keep diagnostics separate from late-pipeline success. */
                return ae_hook(ae_opaque);
            }
        }
    }
    return 0;
}

static void note_late_error(struct fh_isp_runtime *rt,int rc,uint32_t address)
{
    if (!rc) return;
    if (!rt->stage_error_count) {
        rt->last_stage_error = rc;
        rt->last_stage_address = address;
    }
    rt->stage_error_count++;
}

/* Proven CB970 subset. API_ISP_LoadIspParam dirty state is consumed by the
 * periodic pipeline and eventually cleared by CED28. Missing stages remain
 * explicit below instead of being replaced by captured constants. */
int fh_isp_runtime_tick_with_control_hooks(struct fh_isp_runtime *rt,
                                           fh_isp_runtime_ae_hook_fn ae_hook,
                                           void *ae_opaque,
                                           fh_isp_runtime_awb_hook_fn awb_hook,
                                           void *awb_opaque)
{
    int rc;
    if (!rt || !rt->mmio) return -EINVAL;
    rt->last_ae_error = 0;
    rt->last_awb_error = 0;
    rt->last_stage_error = 0;
    rt->last_stage_address = 0;
    rt->stage_error_count = 0;

    if (!rt->stock_runtime_started) {
        rc = fh_isp_runtime_cb890_init_ae_state(rt);
        if (rc) return rc;
        rc = fh_isp_runtime_cb890_init_cb6c4(rt, 0u);
        if (rc) return rc;
        rc = fh_isp_runtime_cb890_init_cfd00(rt);
        if (rc) return rc;
        rc = fh_isp_runtime_cb890_init_d0460(rt);
        if (rc) return rc;
        rt->stock_runtime_started = 1;
        return 0;
    }

    rt->last_ae_error = tick_control_stage(rt,ae_hook,ae_opaque);
    if (awb_hook) {
        /* CB9E0 -> CB9E4 has no return-value branch. Our safe rejection of
         * invalid estimator/provider inputs must not freeze BLC/CCM/gamma. */
        rt->last_awb_error = awb_hook(awb_opaque);
    }
    rc = fh_isp_runtime_apply_ce430(rt);   /* CE430 */
    note_late_error(rt,rc,0xce430u);
    if (fh_isp_runtime_nr3d_ready(rt)) {
        rc = fh_isp_runtime_apply_d0fec(rt); /* D0FEC/NR3D: exact stock slot */
        note_late_error(rt,rc,0xd0fecu);
    }
    rc = fh_isp_runtime_apply_d0e5c(rt);   /* D0E5C */
    note_late_error(rt,rc,0xd0e5cu);
    rc = fh_isp_runtime_apply_ce764(rt);   /* CE764 */
    note_late_error(rt,rc,0xce764u);
    rc = fh_isp_runtime_apply_cfd70(rt);   /* CFD70 */
    note_late_error(rt,rc,0xcfd70u);
    rc = fh_isp_runtime_apply_d1db0_lut(rt); /* D1DB0 exact LUT-dependent +5cc portion */
    note_late_error(rt,rc,0xd1db0u);
    rc = fh_isp_runtime_apply_cfbc4(rt);   /* CFBC4 */
    note_late_error(rt,rc,0xcfbc4u);
    rc = fh_isp_runtime_apply_d2074(rt);  /* D2074 */
    note_late_error(rt,rc,0xd2074u);
    rc = fh_isp_runtime_apply_ceacc(rt);   /* CEACC */
    note_late_error(rt,rc,0xceaccu);
    rc = fh_isp_runtime_apply_cdd6c(rt); /* CDD6C/APC all profiles */
    note_late_error(rt,rc,0xcdd6cu);
    rc = fh_isp_runtime_apply_ce7d8(rt);   /* CE7D8 */
    note_late_error(rt,rc,0xce7d8u);
    rc = fh_isp_runtime_apply_ced28(rt);   /* CED28: consume dirty after prior users */
    note_late_error(rt,rc,0xced28u);
    rc = fh_isp_runtime_apply_d0df4_control(rt); /* D0DF4 exact top-level control */
    note_late_error(rt,rc,0xd0df4u);
    rc = fh_isp_runtime_apply_dynamic_ltm(rt); /* D0528/D0630 live LTM */
    note_late_error(rt,rc,0xd0630u);
    rc = fh_isp_runtime_apply_d0b2c(rt); /* D0B2C all profile rows */
    note_late_error(rt,rc,0xd0b2cu);
    rc = fh_isp_runtime_apply_d1258(rt);   /* D1258 */
    note_late_error(rt,rc,0xd1258u);
    rc = fh_isp_runtime_apply_d0238(rt);   /* D0238/CFEB0 */
    note_late_error(rt,rc,0xd0238u);
    rc = fh_isp_runtime_apply_cecf0(rt);   /* CECF0 */
    note_late_error(rt,rc,0xcecf0u);
    rc = fh_isp_runtime_apply_d1724(rt);   /* D1724; day profile disabled */
    note_late_error(rt,rc,0xd1724u);
    return 0;
}

int fh_isp_runtime_tick_with_awb_hook(struct fh_isp_runtime *rt,
                                      fh_isp_runtime_awb_hook_fn hook,
                                      void *opaque)
{
    return fh_isp_runtime_tick_with_control_hooks(rt, NULL, NULL, hook, opaque);
}

int fh_isp_runtime_tick_proven_subset(struct fh_isp_runtime *rt)
{
    return fh_isp_runtime_tick_with_control_hooks(rt, NULL, NULL, NULL, NULL);
}


/* CE7D8: writer for ISP +0x520/+0x524, enabled by ctx[0x13] bit1.
 * Dynamic mode (ctx+0x94c bit0) uses C5DB8 + D29C0(type=1) on the three
 * profile tables at +0x950/+0x95c/+0x968. */
int fh_isp_runtime_apply_ce7d8(struct fh_isp_runtime *rt)
{
    uint32_t mode, a, b, c, gain, v524;
    int rc;
    if (!rt || !rt->mmio) return -EINVAL;
    if (!(rt->ctx[0x13] & 0x02u)) return 0;

    mode = rd(rt, 0x4f0) & 3u;
    if (rt->ctx[0x94c] & 1u) {
        rc = stock_gain_quantity(rt, &gain);
        if (rc) return rc;
        a = stock_d29c0(rt->ctx + 0x950, gain, 1);
        b = stock_d29c0(rt->ctx + 0x95c, gain, 1);
        c = stock_d29c0(rt->ctx + 0x968, gain, 1);
    } else {
        a = rt->ctx[0x94d];
        b = rt->ctx[0x94e];
        c = rt->ctx[0x94f];
    }

    wr(rt, 0x520, (b << 18) | (c << 2));
    v524 = rd(rt, 0x524) & ~0x0fffu;
    v524 |= (mode | (a << 4)) & 0x0ff3u;
    wr(rt, 0x524, v524);
    return 0;
}

/* CECF0: enabled by ctx[0x13] bit6. Replace only ISP +0x4b8 bits31:24
 * with profile byte ctx+0x25c. */
int fh_isp_runtime_apply_cecf0(struct fh_isp_runtime *rt)
{
    uint32_t v;
    if (!rt || !rt->mmio) return -EINVAL;
    if (!(rt->ctx[0x13] & 0x40u)) return 0;
    v = rd(rt, 0x4b8);
    v = (v & 0x00ffffffu) | ((uint32_t)rt->ctx[0x25c] << 24);
    wr(rt, 0x4b8, v);
    return 0;
}


/* C48CC low-page table output. Source tables were recovered from apollo and
 * independently verified word-for-word against live stock ISP MMIO. */
static const struct fh_reg_init_pair c48cc_low_exact[] = {
    {0x0270, 0x00490000u},
    {0x0274, 0x00d80091u},
    {0x0278, 0x0161011du},
    {0x027c, 0x01e601a4u},
    {0x0280, 0x02660227u},
    {0x0284, 0x02e102a4u},
    {0x0288, 0x0358031du},
    {0x028c, 0x03cb0392u},
    {0x0290, 0x043a0403u},
    {0x0294, 0x04a50470u},
    {0x0298, 0x050c04d9u},
    {0x029c, 0x056f053eu},
    {0x02a0, 0x05cf05a0u},
    {0x02a4, 0x062b05feu},
    {0x02a8, 0x06850658u},
    {0x02ac, 0x06db06b0u},
    {0x02b0, 0x07a50705u},
    {0x02b4, 0x08c4083au},
    {0x02b8, 0x09bd0945u},
    {0x02bc, 0x0a940a2du},
    {0x02c0, 0x00000af4u},
    {0x02c4, 0x00200000u},
    {0x02c8, 0x00600040u},
    {0x02cc, 0x00a00080u},
    {0x02d0, 0x00e000c0u},
    {0x02d4, 0x01200100u},
    {0x02d8, 0x01600140u},
    {0x02dc, 0x01a00180u},
    {0x02e0, 0x01e001c0u},
    {0x02e4, 0x02400200u},
    {0x02e8, 0x02c00280u},
    {0x02ec, 0x03400300u},
    {0x02f0, 0x03c00380u},
    {0x02f4, 0x05000400u},
    {0x02f8, 0x07000600u},
    {0x02fc, 0x09000800u},
    {0x0300, 0x0b000a00u},
    {0x0304, 0x0d000c00u},
    {0x0308, 0x0f000e00u},
    {0x030c, 0x00000ffcu},
};

/* C48CC high-page source at apollo VA 0x2B011C: 385 monotonic 12-bit
 * halfwords. C48CC packs adjacent samples into 384 words at 0x3A00..0x3FFC. */
static const uint16_t c48cc_high_curve[385] = {
    0x000, 0x000, 0x100, 0x196, 0x200, 0x252, 0x296, 0x2cf, 0x300, 0x32c, 0x352, 0x376,
    0x396, 0x3b3, 0x3cf, 0x3e8, 0x400, 0x416, 0x42c, 0x43f, 0x452, 0x464, 0x476, 0x486,
    0x496, 0x4a5, 0x4b3, 0x4c1, 0x4cf, 0x4dc, 0x4e8, 0x4f4, 0x500, 0x50b, 0x516, 0x521,
    0x52c, 0x536, 0x53f, 0x549, 0x552, 0x55c, 0x564, 0x56d, 0x576, 0x57e, 0x586, 0x58e,
    0x596, 0x59d, 0x5a5, 0x5ac, 0x5b3, 0x5ba, 0x5c1, 0x5c8, 0x5cf, 0x5d5, 0x5dc, 0x5e2,
    0x5e8, 0x5ee, 0x5f4, 0x5fa, 0x600, 0x60b, 0x616, 0x621, 0x62c, 0x636, 0x63f, 0x649,
    0x652, 0x65c, 0x664, 0x66d, 0x676, 0x67e, 0x686, 0x68e, 0x696, 0x69d, 0x6a5, 0x6ac,
    0x6b3, 0x6ba, 0x6c1, 0x6c8, 0x6cf, 0x6d5, 0x6dc, 0x6e2, 0x6e8, 0x6ee, 0x6f4, 0x6fa,
    0x700, 0x70b, 0x716, 0x721, 0x72c, 0x736, 0x73f, 0x749, 0x752, 0x75c, 0x764, 0x76d,
    0x776, 0x77e, 0x786, 0x78e, 0x796, 0x79d, 0x7a5, 0x7ac, 0x7b3, 0x7ba, 0x7c1, 0x7c8,
    0x7cf, 0x7d5, 0x7dc, 0x7e2, 0x7e8, 0x7ee, 0x7f4, 0x7fa, 0x800, 0x80b, 0x816, 0x821,
    0x82c, 0x836, 0x83f, 0x849, 0x852, 0x85c, 0x864, 0x86d, 0x876, 0x87e, 0x886, 0x88e,
    0x896, 0x89d, 0x8a5, 0x8ac, 0x8b3, 0x8ba, 0x8c1, 0x8c8, 0x8cf, 0x8d5, 0x8dc, 0x8e2,
    0x8e8, 0x8ee, 0x8f4, 0x8fa, 0x900, 0x90b, 0x916, 0x921, 0x92c, 0x936, 0x93f, 0x949,
    0x952, 0x95c, 0x964, 0x96d, 0x976, 0x97e, 0x986, 0x98e, 0x996, 0x99d, 0x9a5, 0x9ac,
    0x9b3, 0x9ba, 0x9c1, 0x9c8, 0x9cf, 0x9d5, 0x9dc, 0x9e2, 0x9e8, 0x9ee, 0x9f4, 0x9fa,
    0xa00, 0xa0b, 0xa16, 0xa21, 0xa2c, 0xa36, 0xa3f, 0xa49, 0xa52, 0xa5c, 0xa64, 0xa6d,
    0xa76, 0xa7e, 0xa86, 0xa8e, 0xa96, 0xa9d, 0xaa5, 0xaac, 0xab3, 0xaba, 0xac1, 0xac8,
    0xacf, 0xad5, 0xadc, 0xae2, 0xae8, 0xaee, 0xaf4, 0xafa, 0xb00, 0xb0b, 0xb16, 0xb21,
    0xb2c, 0xb36, 0xb3f, 0xb49, 0xb52, 0xb5c, 0xb64, 0xb6d, 0xb76, 0xb7e, 0xb86, 0xb8e,
    0xb96, 0xb9d, 0xba5, 0xbac, 0xbb3, 0xbba, 0xbc1, 0xbc8, 0xbcf, 0xbd5, 0xbdc, 0xbe2,
    0xbe8, 0xbee, 0xbf4, 0xbfa, 0xc00, 0xc0b, 0xc16, 0xc21, 0xc2c, 0xc36, 0xc3f, 0xc49,
    0xc52, 0xc5c, 0xc64, 0xc6d, 0xc76, 0xc7e, 0xc86, 0xc8e, 0xc96, 0xc9d, 0xca5, 0xcac,
    0xcb3, 0xcba, 0xcc1, 0xcc8, 0xccf, 0xcd5, 0xcdc, 0xce2, 0xce8, 0xcee, 0xcf4, 0xcfa,
    0xd00, 0xd0b, 0xd16, 0xd21, 0xd2c, 0xd36, 0xd3f, 0xd49, 0xd52, 0xd5c, 0xd64, 0xd6d,
    0xd76, 0xd7e, 0xd86, 0xd8e, 0xd96, 0xd9d, 0xda5, 0xdac, 0xdb3, 0xdba, 0xdc1, 0xdc8,
    0xdcf, 0xdd5, 0xddc, 0xde2, 0xde8, 0xdee, 0xdf4, 0xdfa, 0xe00, 0xe0b, 0xe16, 0xe21,
    0xe2c, 0xe36, 0xe3f, 0xe49, 0xe52, 0xe5c, 0xe64, 0xe6d, 0xe76, 0xe7e, 0xe86, 0xe8e,
    0xe96, 0xe9d, 0xea5, 0xeac, 0xeb3, 0xeba, 0xec1, 0xec8, 0xecf, 0xed5, 0xedc, 0xee2,
    0xee8, 0xeee, 0xef4, 0xefa, 0xf00, 0xf0b, 0xf16, 0xf21, 0xf2c, 0xf36, 0xf3f, 0xf49,
    0xf52, 0xf5c, 0xf64, 0xf6d, 0xf76, 0xf7e, 0xf86, 0xf8e, 0xf96, 0xf9d, 0xfa5, 0xfac,
    0xfb3, 0xfba, 0xfc1, 0xfc8, 0xfcf, 0xfd5, 0xfdc, 0xfe2, 0xfe8, 0xfee, 0xff4, 0xffa,
    0xfff,
};

int fh_isp_runtime_apply_c48cc_known(struct fh_isp_runtime *rt)
{
    size_t i;
    if (!rt || !rt->mmio) return -EINVAL;
    /* Regression-proven v3.8 C48CC programming.
     * Hardware readback for this aperture is zero/write-only, but removing these
     * 384 writes in v4.0.x caused PAE frames to explode from a few KiB to
     * ~0.4-0.6 MiB and corrupt decode. Restore the exact v3.8 behavior. */
    for (i=0; i<sizeof(c48cc_low_exact)/sizeof(c48cc_low_exact[0]); ++i)
        wr(rt, c48cc_low_exact[i].off, c48cc_low_exact[i].val);
    for (i=0; i<384; ++i) {
        uint32_t v=(uint32_t)c48cc_high_curve[i] | ((uint32_t)c48cc_high_curve[i+1] << 16);
        wr(rt, 0x3a00u + (unsigned)i*4u, v);
    }
    return 0;
}

/* CFB64 from API_ISP_Run: exact day-profile LUT propagation.
 * cfg+0x5f0 -> ISP 0x1480 bank; cfg+0x730 -> 0x1000/0x1180/0x1300.
 * Both 80-word source tables are byte-identical between the GC1054 day SREG
 * payload and the captured live stock ISP parameter context. */
int fh_isp_runtime_apply_profile_luts(struct fh_isp_runtime *rt)
{
    size_t i;
    if (!rt || !rt->mmio) return -EINVAL;
    for (i = 0; i < 80; ++i) {
        uint32_t a, b;
        memcpy(&a, rt->ctx + 0x5f0 + i * 4, 4);
        memcpy(&b, rt->ctx + 0x730 + i * 4, 4);
        wr(rt, 0x1480 + (unsigned)i * 4, a);
        wr(rt, 0x1000 + (unsigned)i * 4, b);
        wr(rt, 0x1180 + (unsigned)i * 4, b);
        wr(rt, 0x1300 + (unsigned)i * 4, b);
    }
    return 0;
}

int fh_isp_runtime_apply_known_stock_init(struct fh_isp_runtime *rt, unsigned width, unsigned height)
{
    int rc;
    if (!rt || !rt->mmio) return -EINVAL;
    /* C5854 runs immediately before C540C in the stock API_ISP_Init path.
     * Besides publishing the global context pointer it sets ctx+0xa70 = 1;
     * C5DB8 requires this validity flag before gain-driven runtime writers. */
    ctx_set_u32(rt, 0x0a70, 1u);
    rc=fh_isp_runtime_apply_c4998_static_defaults(rt); if(rc) return rc;
    rc=fh_isp_runtime_apply_c48cc_known(rt); if(rc) return rc;
    /* C5078 initial x4 LUT pointer remains unresolved; profile banks are seeded by exact CFB64 after ISP_START. */
    c4998_finish_defaults(rt);
    /* In the real lifecycle BF368 has already populated the three geometry
     * pairs.  Fall back to the explicit native dimensions only if a legacy
     * caller skipped BF368. */
    if (!(ctx_u16(rt,0x14)&0x0fffu) || !(ctx_u16(rt,0x16)&0x0fffu)) {
        ctx_set_u16(rt,0x14,(uint16_t)width); ctx_set_u16(rt,0x16,(uint16_t)height);
        ctx_set_u16(rt,0x20,(uint16_t)width); ctx_set_u16(rt,0x22,(uint16_t)height);
    }
    rc=fh_isp_runtime_apply_c531c(rt); if(rc) return rc;
    return fh_isp_runtime_finish_core_init(rt);
}
