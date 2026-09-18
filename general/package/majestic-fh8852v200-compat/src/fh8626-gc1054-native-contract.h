#ifndef FH8626_GC1054_NATIVE_CONTRACT_H
#define FH8626_GC1054_NATIVE_CONTRACT_H

#include <stddef.h>
#include <stdint.h>

/*
 * Exact FH8626V100 / GC1054 contracts recovered from the retained stock
 * libgc1054_mipi.so + libmipi.so Ghidra programs on 2026-09-18.
 *
 * Evidence class: REVERSE_CONFIRMED. This is not target acceptance of a new
 * implementation.
 */

#define FH8626_GC1054_FORMAT_720P20        0x80104e20u
#define FH8626_GC1054_FORMAT_720P166666    0x8010411au
#define FH8626_GC1054_FORMAT_720P15        0x80103a98u
#define FH8626_GC1054_FORMAT_720P30        0x80107530u
#define FH8626_GC1054_FORMAT_720P30_ALIAS  4u
#define FH8626_GC1054_FORMAT_720P25        0x801061a8u
#define FH8626_GC1054_FORMAT_720P25_ALIAS  3u

#define FH8626_GC1054_I2C_DEVICE          "/dev/i2c-0"
#define FH8626_GC1054_I2C_MSG_ADDR        0x21u
#define FH8626_GC1054_I2C_IOCTL_TENBIT    0x704ul
#define FH8626_GC1054_I2C_IOCTL_FORCE     0x706ul
#define FH8626_GC1054_I2C_IOCTL_RDWR      0x707ul
#define FH8626_GC1054_I2C_FORCE_ARG       0x1au

#define FH8626_GC1054_MIPI_MAP0_BASE      0xf0000000u
#define FH8626_GC1054_MIPI_MAP0_SIZE      0x38u
#define FH8626_GC1054_MIPI_MAP1_BASE      0xf0002000u
#define FH8626_GC1054_MIPI_MAP1_SIZE      0x44u
#define FH8626_GC1054_MIPI_MAP2_BASE      0xf1000000u
#define FH8626_GC1054_MIPI_MAP2_SIZE      0x0cu
#define FH8626_GC1054_MIPI_MAP3_BASE      0xf1100000u
#define FH8626_GC1054_MIPI_MAP3_SIZE      0x14cu

/* Exact six-word argument supplied by the GC1054 Sensor_Init callback. */
static const uint32_t fh8626_gc1054_mipi_init_words[6] = {
    5u, 0u, 0u, 0u, 0u, 1u
};

/*
 * Raw 24-byte get_vi_attr result. Keep the layout raw until every field name
 * is proven by the FH8626 ISP consumer. All five formats share the same
 * 1280x720 geometry and line length; word16[0] is the base frame length.
 */
struct fh8626_gc1054_vi_attr_raw {
    uint16_t word16[8];
    uint32_t word32_10;
    uint32_t orientation_word14;
};

struct fh8626_gc1054_format_contract {
    uint32_t format;
    uint32_t numeric_alias;
    uint16_t frame_length;
    uint16_t vblank;
    double nominal_fps;
};

static const struct fh8626_gc1054_format_contract
fh8626_gc1054_formats[] = {
    {FH8626_GC1054_FORMAT_720P20,     0u,                              1125u, 0x0185u, 20.0},
    {FH8626_GC1054_FORMAT_720P166666, 0u,                              1352u, 0x0268u, 16.6666},
    {FH8626_GC1054_FORMAT_720P15,     0u,                              1500u, 0x02fcu, 15.0},
    {FH8626_GC1054_FORMAT_720P30,     FH8626_GC1054_FORMAT_720P30_ALIAS, 749u, 0x000du, 30.0},
    {FH8626_GC1054_FORMAT_720P25,     FH8626_GC1054_FORMAT_720P25_ALIAS, 899u, 0x00a3u, 25.0},
};

#define FH8626_GC1054_FORMAT_COUNT \
    (sizeof(fh8626_gc1054_formats) / sizeof(fh8626_gc1054_formats[0]))

static inline const struct fh8626_gc1054_format_contract *
fh8626_gc1054_find_format(uint32_t format)
{
    size_t i;
    for (i = 0; i < FH8626_GC1054_FORMAT_COUNT; ++i) {
        const struct fh8626_gc1054_format_contract *f =
            &fh8626_gc1054_formats[i];
        if (format == f->format ||
            (f->numeric_alias && format == f->numeric_alias))
            return f;
    }
    return NULL;
}

static inline struct fh8626_gc1054_vi_attr_raw
fh8626_gc1054_build_vi_attr(const struct fh8626_gc1054_format_contract *f,
                            int orientation_nonzero)
{
    struct fh8626_gc1054_vi_attr_raw a = {
        {0u, 0x06beu, 720u, 1280u, 0u, 0u, 720u, 1280u},
        0u,
        0u
    };
    if (f)
        a.word16[0] = f->frame_length;
    if (orientation_nonzero)
        a.orientation_word14 = 2u;
    return a;
}

static const uint32_t fh8626_gc1054_bayer_map_normal[4] = {0u, 3u, 1u, 2u};
static const uint32_t fh8626_gc1054_bayer_map_oriented[4] = {2u, 1u, 3u, 0u};

struct fh8626_gc1054_reg_write {
    uint16_t reg;
    uint16_t value;
};

/*
 * Exact 145-write 1280x720@25 format array.
 *
 * Full-library reverse corrected an earlier boundary mistake: there is no
 * trailing sentinel. Each stock format is a contiguous 0x244-byte array of
 * 145 {uint16 register,uint16 value} pairs. GCC hoisted the first {0xf2,0}
 * element into local registers and started the loop pointer at pair 1.
 *
 * Physical arrays:
 *   20 fps      0x13448..0x1368b
 *   16.6666 fps 0x1368c..0x138cf
 *   15 fps      0x138d0..0x13b13
 *   30 fps      0x13b14..0x13d57
 *   25 fps      0x13d58..0x13f9b
 * Bayer mapping data begins at 0x13f9c.
 *
 * Machine comparison in Ghidra proved that the five arrays differ only at
 * pair 13 (register 0x07) and pair 14 (register 0x08). This template is the
 * exact 25-fps array; fh8626_gc1054_format_pair() substitutes those two
 * proven per-format values.
 */
static const struct fh8626_gc1054_reg_write
fh8626_gc1054_720p25_init[] = {
    {0x00f2u, 0x0000u},
    {0x00f6u, 0x0000u},
    {0x00fcu, 0x0004u},
    {0x00f7u, 0x0001u},
    {0x00f8u, 0x000cu},
    {0x00f9u, 0x0006u},
    {0x00fau, 0x0080u},
    {0x00fcu, 0x000eu},
    {0x00feu, 0x0000u},
    {0x0003u, 0x0002u},
    {0x0004u, 0x00a6u},
    {0x0005u, 0x0002u},
    {0x0006u, 0x0007u},
    {0x0007u, 0x0000u},
    {0x0008u, 0x00a3u},
    {0x0009u, 0x0000u},
    {0x000au, 0x0004u},
    {0x000bu, 0x0000u},
    {0x000cu, 0x0000u},
    {0x000du, 0x0002u},
    {0x000eu, 0x00d4u},
    {0x000fu, 0x0005u},
    {0x0010u, 0x0008u},
    {0x0017u, 0x00c0u},
    {0x0018u, 0x0002u},
    {0x0019u, 0x0008u},
    {0x001au, 0x0018u},
    {0x001du, 0x0012u},
    {0x001eu, 0x0050u},
    {0x001fu, 0x0080u},
    {0x0021u, 0x0030u},
    {0x0023u, 0x00f8u},
    {0x0025u, 0x0010u},
    {0x0028u, 0x0020u},
    {0x0034u, 0x000au},
    {0x003cu, 0x0010u},
    {0x003du, 0x000eu},
    {0x00ccu, 0x008eu},
    {0x00cdu, 0x009au},
    {0x00cfu, 0x0070u},
    {0x00d0u, 0x00a9u},
    {0x00d1u, 0x00c5u},
    {0x00d2u, 0x00edu},
    {0x00d8u, 0x003cu},
    {0x00d9u, 0x007au},
    {0x00dau, 0x0012u},
    {0x00dbu, 0x0050u},
    {0x00deu, 0x000cu},
    {0x00e3u, 0x0060u},
    {0x00e4u, 0x0078u},
    {0x00feu, 0x0001u},
    {0x00e3u, 0x0001u},
    {0x00e6u, 0x0010u},
    {0x00feu, 0x0001u},
    {0x0080u, 0x0040u},
    {0x0088u, 0x0073u},
    {0x0089u, 0x0003u},
    {0x0090u, 0x0001u},
    {0x0092u, 0x0002u},
    {0x0094u, 0x0003u},
    {0x0095u, 0x0002u},
    {0x0096u, 0x00d0u},
    {0x0097u, 0x0005u},
    {0x0098u, 0x0000u},
    {0x00feu, 0x0001u},
    {0x0040u, 0x0022u},
    {0x0043u, 0x0003u},
    {0x004eu, 0x003cu},
    {0x004fu, 0x0000u},
    {0x0060u, 0x0000u},
    {0x0061u, 0x0080u},
    {0x00feu, 0x0001u},
    {0x00b0u, 0x0048u},
    {0x00b1u, 0x0001u},
    {0x00b2u, 0x0000u},
    {0x00b6u, 0x0000u},
    {0x00feu, 0x0002u},
    {0x0001u, 0x0000u},
    {0x0002u, 0x0001u},
    {0x0003u, 0x0002u},
    {0x0004u, 0x0003u},
    {0x0005u, 0x0004u},
    {0x0006u, 0x0005u},
    {0x0007u, 0x0006u},
    {0x0008u, 0x000eu},
    {0x0009u, 0x0016u},
    {0x000au, 0x001eu},
    {0x000bu, 0x0036u},
    {0x000cu, 0x003eu},
    {0x000du, 0x0056u},
    {0x00feu, 0x0002u},
    {0x00b0u, 0x0000u},
    {0x00b1u, 0x0000u},
    {0x00b2u, 0x0000u},
    {0x00b3u, 0x0011u},
    {0x00b4u, 0x0022u},
    {0x00b5u, 0x0054u},
    {0x00b6u, 0x00b8u},
    {0x00b7u, 0x0060u},
    {0x00b9u, 0x0000u},
    {0x00bau, 0x00c0u},
    {0x00c0u, 0x0020u},
    {0x00c1u, 0x002du},
    {0x00c2u, 0x0040u},
    {0x00c3u, 0x005bu},
    {0x00c4u, 0x0080u},
    {0x00c5u, 0x00b5u},
    {0x00c6u, 0x0000u},
    {0x00c7u, 0x006au},
    {0x00c8u, 0x0000u},
    {0x00c9u, 0x00d4u},
    {0x00cau, 0x0000u},
    {0x00cbu, 0x00a8u},
    {0x00ccu, 0x0000u},
    {0x00cdu, 0x0050u},
    {0x00ceu, 0x0000u},
    {0x00cfu, 0x00a1u},
    {0x00feu, 0x0002u},
    {0x0054u, 0x00f7u},
    {0x0055u, 0x00f0u},
    {0x0056u, 0x0000u},
    {0x0057u, 0x0000u},
    {0x0058u, 0x0000u},
    {0x005au, 0x0004u},
    {0x00feu, 0x0004u},
    {0x0081u, 0x008au},
    {0x00feu, 0x0003u},
    {0x0001u, 0x0003u},
    {0x0002u, 0x0055u},
    {0x0003u, 0x0090u},
    {0x0010u, 0x0090u},
    {0x0011u, 0x002bu},
    {0x0012u, 0x0040u},
    {0x0013u, 0x0006u},
    {0x0015u, 0x0000u},
    {0x0021u, 0x0002u},
    {0x0022u, 0x0002u},
    {0x0023u, 0x0008u},
    {0x0024u, 0x0002u},
    {0x0025u, 0x0010u},
    {0x0026u, 0x0004u},
    {0x0029u, 0x0003u},
    {0x002au, 0x0002u},
    {0x002bu, 0x0004u},
    {0x00feu, 0x0000u},
};

#define FH8626_GC1054_720P25_INIT_COUNT \
    (sizeof(fh8626_gc1054_720p25_init) / sizeof(fh8626_gc1054_720p25_init[0]))

static inline struct fh8626_gc1054_reg_write
fh8626_gc1054_format_pair(const struct fh8626_gc1054_format_contract *f,
                          size_t index)
{
    struct fh8626_gc1054_reg_write p = {0u, 0u};
    if (!f || index >= FH8626_GC1054_720P25_INIT_COUNT)
        return p;
    p = fh8626_gc1054_720p25_init[index];
    if (index == 13u)
        p.value = (uint16_t)(f->vblank >> 8);
    else if (index == 14u)
        p.value = (uint16_t)(f->vblank & 0xffu);
    return p;
}

struct fh8626_gc1054_gain_program {
    uint8_t write_triplet;
    uint8_t b6;
    uint8_t b1;
    uint8_t b2;
    uint8_t page4_40;
};

static inline uint32_t fh8626_gc1054_mul_hi_u32(uint32_t a, uint32_t b)
{
    return (uint32_t)(((uint64_t)a * b) >> 32);
}

/*
 * Exact Gc1054SetGain arithmetic reconstructed from ARM instructions at
 * 0x118d4.  The caller writes page 1 before the optional B6/B1/B2 triplet,
 * then page 4 register 0x40, then returns to page 0.
 */
static inline struct fh8626_gc1054_gain_program
fh8626_gc1054_gain_program(uint32_t gain)
{
    struct fh8626_gc1054_gain_program p = {0u, 0u, 0u, 0u, 0u};
    uint32_t x, hi, y;

    if (gain >= 0x40u) {
        p.write_triplet = 1u;
        p.b1 = 1u;

        if (gain <= 0x5au) {
            p.b6 = 0u;
            p.b2 = (uint8_t)((gain << 2) & 0xfcu);
        } else if (gain <= 0x7eu) {
            p.b6 = 1u;
            x = gain << 6;
            hi = fh8626_gc1054_mul_hi_u32(0x68168169u, x);
            y = hi + ((x - hi) >> 1);
            p.b2 = (uint8_t)((y >> 4) & 0xfcu);
        } else if (gain <= 0xb5u) {
            p.b6 = 2u;
            x = gain << 6;
            hi = fh8626_gc1054_mul_hi_u32(0x02040811u, x);
            y = hi + ((x - hi) >> 1);
            p.b2 = (uint8_t)((y >> 4) & 0xfcu);
        } else if (gain <= 0x100u) {
            p.b6 = 3u;
            x = gain << 5;
            hi = fh8626_gc1054_mul_hi_u32(0xb40b40b5u, x);
            p.b2 = (uint8_t)((hi >> 4) & 0xfcu);
        } else if (gain <= 0x170u) {
            p.b6 = 4u;
            hi = fh8626_gc1054_mul_hi_u32(0xff00ff01u, gain << 6);
            p.b2 = (uint8_t)((hi >> 6) & 0xfcu);
        } else if (gain <= 0x202u) {
            p.b6 = 5u;
            hi = fh8626_gc1054_mul_hi_u32(0xb19ab5c5u, gain << 6);
            p.b2 = (uint8_t)((hi >> 6) & 0xfcu);
        } else if (gain <= 0x2e0u) {
            p.b6 = 6u;
            hi = fh8626_gc1054_mul_hi_u32(0x7f411e53u, gain << 6);
            p.b2 = (uint8_t)((hi >> 6) & 0xfcu);
        } else if (gain <= 0x406u) {
            p.b6 = 7u;
            x = gain << 6;
            hi = fh8626_gc1054_mul_hi_u32(0x63b0cda3u, x);
            y = hi + ((x - hi) >> 1);
            p.b2 = (uint8_t)((y >> 7) & 0xfcu);
        } else if (gain <= 0x5d2u) {
            p.b6 = 8u;
            hi = fh8626_gc1054_mul_hi_u32(0x7f218557u, gain << 6);
            p.b2 = (uint8_t)((hi >> 7) & 0xfcu);
        } else if (gain <= 0x823u) {
            p.b6 = 9u;
            hi = fh8626_gc1054_mul_hi_u32(0x15fa298du, gain << 6);
            p.b2 = (uint8_t)((hi >> 5) & 0xfcu);
        } else {
            p.b6 = 10u;
            hi = fh8626_gc1054_mul_hi_u32(0xfb93e673u, gain << 6);
            p.b1 = (uint8_t)(hi >> 17);
            p.b2 = (uint8_t)(((hi >> 11) << 2) & 0xfcu);
        }
    }

    if (gain < 0x300u)
        p.page4_40 = 0u;
    else if (gain < 0x320u)
        p.page4_40 = 3u;
    else if (gain < 0x340u)
        p.page4_40 = 4u;
    else
        p.page4_40 = 8u;

    return p;
}

static inline void fh8626_gc1054_integration_regs(uint32_t integration,
    uint8_t *reg03, uint8_t *reg04)
{
    if (reg03)
        *reg03 = (uint8_t)((integration >> 8) & 0xffu);
    if (reg04)
        *reg04 = (uint8_t)(integration & 0xffu);
}

/* Active height is 720 and stock subtracts an additional 16-line margin
 * before programming page-0 vertical blanking registers 0x07/0x08. */
static inline uint32_t
fh8626_gc1054_format_frame_length_from_multiplier(
    const struct fh8626_gc1054_format_contract *f, uint32_t multiplier)
{
    return f ? multiplier * (uint32_t)f->frame_length : 0u;
}

static inline uint32_t fh8626_gc1054_frame_length_from_multiplier(uint32_t multiplier)
{
    return multiplier * 899u;
}

static inline uint16_t fh8626_gc1054_vblank_from_frame_length(uint32_t frame_length)
{
    return (uint16_t)(frame_length - 736u);
}

#endif
