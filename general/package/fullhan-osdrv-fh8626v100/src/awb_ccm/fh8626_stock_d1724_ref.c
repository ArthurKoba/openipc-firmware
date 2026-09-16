#include <stdint.h>
#include <string.h>
#include "fh8626_stock_d1724_ref.h"

static uint16_t rd16(const uint8_t *p) { uint16_t v; memcpy(&v,p,2); return v; }
static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v,p,4); return v; }
static int32_t sx13(uint16_t v)
{
    uint32_t x = v & 0x1fffu;
    return (x & 0x1000u) ? (int32_t)(x | 0xffffe000u) : (int32_t)x;
}

void fh8626_d1648_copy_stats(struct fh8626_d1724_stat24 dst[32],
                             const struct fh8626_d1724_stat24 src[32])
{
    memcpy(dst, src, 32u * sizeof(*dst));
}

void fh8626_d1724_sum_stats(const struct fh8626_d1724_stat24 st[32],
                            struct fh8626_d1724_sums *o)
{
    unsigned i;
    uint32_t den;
    memset(o,0,sizeof(*o));
    for (i=0;i<32u;++i) {
        o->s0 += st[i].v0; o->s1 += st[i].v1; o->s2 += st[i].v2;
        o->s3 += st[i].v3; o->s4 += st[i].v4;
    }
    den = o->s3 * 3u + 1u;
    o->norm = den ? (uint32_t)((((uint64_t)o->s0 + o->s1 + o->s2) << 8) / den) : 0xffffffffu;
    o->reciprocal = o->norm ? 0x20000u / o->norm : 0u;
}

void fh8626_d16a4_apply_row(uint32_t isp_words[], const uint8_t ctx[],
                              const uint32_t rows[8][6])
{
    unsigned row, i;
    if (!rows || (ctx[0x999] & 0x10u)) return;
    row = ctx[0x999] >> 5;
    for (i=0;i<6u;++i) isp_words[(0x40cu/4u)+i] = rows[row][i];
}

static uint32_t range_recip_15(uint32_t lo, uint32_t hi)
{
    return lo < hi ? 0x8000u/(hi-lo) : 0x7fffu;
}

void fh8626_d1724_apply_range_tail(uint32_t isp[], const uint8_t ctx[])
{
    uint32_t x0=rd16(ctx+0x9e8)&0xfffu, x1=rd16(ctx+0x9ea)&0xfffu;
    uint32_t y0=rd16(ctx+0x9ec)&0xfffu, y1=rd16(ctx+0x9ee)&0xfffu;
    uint32_t a=rd16(ctx+0x9f4)&0xfffu, b=rd16(ctx+0x9f6)&0xfffu;
    uint32_t scale=0xfffu;
    isp[0x424/4] = x0 | (x1<<16);
    isp[0x42c/4] = y0 | (y1<<16);
    isp[0x428/4] = range_recip_15(x0,x1) | (range_recip_15(y0,y1)<<16);
    isp[0x430/4] = rd32(ctx+0x9f0);
    if (a != b) {
        uint32_t q = 0x10000u / (a-b); /* stock configuration expects a>b */
        if (q <= 0xffeu) scale=q;
    }
    isp[0x3c8/4] = scale | (b<<16);
    isp[0x3b4/4] = (isp[0x3b4/4] & ~0x10u) | ((ctx[0x99a]&1u)<<4);
}

void fh8626_d1724_apply_default(uint32_t isp[], const uint8_t ctx[],
                                const struct fh8626_d1724_sums *s)
{
    isp[0x3c0/4]=0x02000100u; isp[0x3c4/4]=0x02000100u;
    isp[0x3bc/4]=(s->reciprocal<<16)|s->norm;
    isp[0x3b4/4]&=~0x100u;
    isp[0x3cc/4]=0x200u;      isp[0x3d0/4]=0u;          isp[0x3d4/4]=0x02000000u;
    isp[0x3d8/4]=0u;          isp[0x3dc/4]=0u;          isp[0x3e0/4]=0x200u;
    isp[0x3e4/4]=0u;          isp[0x3e8/4]=0x02000000u; isp[0x3ec/4]=0x200u;
    isp[0x3f0/4]=0u;          isp[0x3f4/4]=0x02000000u; isp[0x3f8/4]=0u;
    isp[0x3fc/4]=0u;          isp[0x400/4]=0x200u;      isp[0x404/4]=0u;
    isp[0x408/4]=0x02000000u;
    fh8626_d1724_apply_range_tail(isp,ctx);
}

int32_t fh8626_d1724_interp_s13_q14(uint16_t a13, uint16_t b13, uint32_t w)
{
    int64_t q14 = (int64_t)(int32_t)w * sx13(a13) +
                  (int64_t)(0x4000u-w) * sx13(b13);
    return (int32_t)(q14 >> 14);
}


static void wr16(uint8_t *p, uint16_t v) { memcpy(p,&v,2); }

static uint32_t abs_i32_u32(int32_t x)
{
    return x < 0 ? (uint32_t)(-(int64_t)x) : (uint32_t)x;
}

static uint32_t div_s32_nonneg(uint32_t num, uint32_t den)
{
    return den ? num / den : 0xffffffffu;
}

/* Exact D1724 adaptive normalization tracker, reconstructed from
 * d1804..d18ec + d1c78..d1d50.  This mutates ctx+A34/A36/A37 and the low12
 * shadow sample in ctx+998, exactly like the stock persistent state. */
static uint32_t d1724_track_norm(uint8_t ctx[], uint32_t norm)
{
    uint8_t ctl99a = ctx[0x99a];
    unsigned shift = ctx[0x99c] & 0x0fu;
    unsigned wide_thresh = ctx[0x99c] >> 4;
    unsigned delay_limit = (rd32(ctx+0x99c) >> 12) & 0xffu;
    unsigned narrow_thresh = ctx[0x99d] & 0x0fu;
    unsigned step = ctx[0x99e] >> 4;
    uint8_t delay = ctx[0xa36];
    uint8_t sample_delay = ctx[0xa37];
    uint16_t acc16 = rd16(ctx+0xa34);
    uint32_t target = norm << shift;
    uint32_t selected = target;
    uint32_t acc = acc16;
    uint32_t absdiff;
    int32_t delta;

    if ((ctl99a >> 1) & 1u) {
        /* Stock one-shot fast initialization consumes ctx99A bit1. */
        ctx[0x99a] = (uint8_t)(ctl99a & ~0x02u);
        delay = 0;
        sample_delay = 0;
        acc = target;
        {
            uint16_t h = rd16(ctx+0x998);
            h = (uint16_t)((h & 0xf000u) | ((target >> shift) & 0x0fffu));
            wr16(ctx+0x998,h);
        }
    } else {
        /* First debounce: decide whether to accept the newly computed target
         * or keep the previous 12-bit shadow sample from ctx+998. */
        if (ctx[0x99f] >= sample_delay) {
            uint32_t shadow = ((uint32_t)(rd16(ctx+0x998) & 0x0fffu)) << shift;
            uint32_t target_delta = abs_i32_u32((int32_t)(shadow - target));
            selected = shadow;
            absdiff = abs_i32_u32((int32_t)(shadow - acc));
            if ((ctx[0x9a0] & 0x0fu) >= target_delta) {
                sample_delay = 0;
            } else {
                sample_delay = (uint8_t)(sample_delay + 1u);
            }
        } else {
            selected = target;
            sample_delay = 0;
            absdiff = abs_i32_u32((int32_t)(selected - acc));
        }

        {
            uint16_t h = rd16(ctx+0x998);
            h = (uint16_t)((h & 0xf000u) | ((selected >> shift) & 0x0fffu));
            wr16(ctx+0x998,h);
        }
        ctx[0xa37] = sample_delay;

        /* Second debounce: delay movement while the error is only beyond the
         * narrow threshold; once delay_limit is exceeded use the wider
         * threshold and step the accumulator toward selected. */
        delta = (int32_t)(selected - acc);
        if (delay_limit >= delay) {
            if (absdiff <= (narrow_thresh << shift)) {
                delay = 0;
            } else {
                delay = (uint8_t)(delay + 1u);
            }
        } else {
            if (absdiff <= (wide_thresh << shift)) {
                delay = 0;
            } else {
                if (delta > 0) acc += step;
                else acc -= step;
            }
        }
    }

    ctx[0xa36] = delay;
    wr16(ctx+0xa34,(uint16_t)acc);
    return (uint32_t)((int32_t)acc >> shift);
}

/* d18fc..d1960: first 0..128 factor from coarse gain and two packed 7-bit
 * boundaries at ctx+9A4. */
static uint32_t d1724_blend_gain(const uint8_t ctx[])
{
    uint32_t packed = rd16(ctx+0x9a4);
    uint32_t lo = ctx[0x9a4] & 0x7fu;
    uint32_t hi = (packed >> 7) & 0x7fu;
    uint32_t span = hi - lo;
    uint32_t gain = rd32(ctx+0x60) >> 18;
    uint32_t x = gain - lo;
    if ((int32_t)x < 0) x = 0;
    if (x > span) x = span;
    if (span < 1u) span = 1u;
    return div_s32_nonneg(x << 7, span);
}

/* d1960..d19b4: second 0..128 factor from statistics fraction and packed
 * 9-bit boundaries spanning ctx+9A4..+9A7. */
static uint32_t d1724_blend_stats(const uint8_t ctx[],
                                  const struct fh8626_d1724_sums *s)
{
    uint32_t denom = s->s0 + s->s1 + s->s2 + s->s4 + 1u;
    uint32_t frac = denom ? (uint32_t)(((uint64_t)s->s4 << 10) / denom) : 0xffffffffu;
    uint32_t lo = (rd32(ctx+0x9a4) >> 14) & 0x1ffu;
    uint32_t hi = (rd16(ctx+0x9a6) >> 7) & 0x1ffu;
    uint32_t span = hi - lo;
    uint32_t x = frac - lo;
    if ((int32_t)x < 0) x = 0;
    if (x > span) x = span;
    if (span < 1u) span = 1u;
    return div_s32_nonneg(x << 7, span);
}

/* d19b8..d1a30 + d1c6c/d1cb0/d1cd0: exact persistent Q14 blend tracker. */
static uint32_t d1724_track_blend(uint8_t ctx[], uint32_t desired, unsigned fast_mode)
{
    uint16_t h = rd16(ctx+0xa38);
    uint32_t current = h & 0x7fffu;
    uint8_t debounce = ctx[0xa3a];
    int32_t delta = (int32_t)(desired - current);

    if (fast_mode) {
        current = desired;
        debounce = 0;
    } else if (ctx[0x9a2] >= debounce) {
        unsigned sh = ctx[0x9a0] >> 4;
        unsigned threshold = ctx[0x9a1] >> 4;
        if (abs_i32_u32(delta) > (threshold << sh))
            debounce = (uint8_t)(debounce + 1u);
        else
            debounce = 0;
    } else {
        unsigned sh = ctx[0x9a0] >> 4;
        unsigned threshold = ctx[0x9a1] & 0x0fu;
        unsigned step = ctx[0x9a3] & 0x0fu;
        if (abs_i32_u32(delta) > (threshold << sh)) {
            if (delta > 0) current += step;
            else current -= step;
            /* Stock preserves current debounce byte after an actual step. */
        } else {
            debounce = 0;
        }
    }

    ctx[0xa3a] = debounce;
    h = (uint16_t)((h & 0x8000u) | (current & 0x7fffu));
    wr16(ctx+0xa38,h);
    return current;
}

static void replace_s13_hi(uint32_t isp[], unsigned off, int32_t v)
{
    uint32_t x = isp[off/4u];
    x = (x & 0xe000ffffu) | (((uint32_t)v & 0x1fffu) << 16);
    isp[off/4u] = x;
}

void fh8626_d1724_apply_adaptive(uint32_t isp[], uint8_t ctx[],
                                 const struct fh8626_d1724_sums *s,
                                 struct fh8626_d1724_adaptive_debug *dbg)
{
    uint32_t tracked, inv, ba, bb, desired, w;
    unsigned fast_mode = (ctx[0x99a] >> 1) & 1u;
    int32_t c0,c1,c2;

    isp[0x3c0/4]=0x02000100u;
    isp[0x3c4/4]=0x02000100u;
    isp[0x3b4/4] |= 0x100u;

    tracked = d1724_track_norm(ctx,s->norm);
    inv = 0x20000u / (tracked >= 1u ? tracked : 1u);
    isp[0x3bc/4] = ((inv << 16) & 0x0fff0000u) | (tracked & 0x07ffu);

    ba = d1724_blend_gain(ctx);
    bb = d1724_blend_stats(ctx,s);
    desired = bb * (128u - ba) + ba * 128u;
    w = d1724_track_blend(ctx,desired,fast_mode);

    c0 = fh8626_d1724_interp_s13_q14(rd16(ctx+0x9ce),rd16(ctx+0x9ae),w);
    c1 = fh8626_d1724_interp_s13_q14(rd16(ctx+0x9d6),rd16(ctx+0x9b6),w);
    c2 = fh8626_d1724_interp_s13_q14(rd16(ctx+0x9de),rd16(ctx+0x9be),w);
    replace_s13_hi(isp,0x3f0,c0); replace_s13_hi(isp,0x3d0,c0);
    replace_s13_hi(isp,0x3f8,c1); replace_s13_hi(isp,0x3d8,c1);
    replace_s13_hi(isp,0x400,c2); replace_s13_hi(isp,0x3e0,c2);

    fh8626_d1724_apply_range_tail(isp,ctx);
    if (dbg) {
        dbg->target_norm_shifted = s->norm << (ctx[0x99c]&0x0fu);
        dbg->tracked_norm = tracked;
        dbg->blend_a=ba; dbg->blend_b=bb;
        dbg->desired_q14=desired; dbg->applied_q14=w;
    }
}
