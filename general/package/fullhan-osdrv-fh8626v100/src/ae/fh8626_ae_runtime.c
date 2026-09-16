#include "fh8626_ae_runtime.h"

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <string.h>

static int32_t ae_positive_trunc(float x);

static uint16_t rd16(const uint8_t *p, unsigned off)
{
    uint16_t v;
    memcpy(&v, p + off, sizeof(v));
    return v;
}

static void wr16(uint8_t *p, unsigned off, uint16_t v)
{
    memcpy(p + off, &v, sizeof(v));
}


/* Apollo D26D0: round finite float to nearest integer, half away from zero.
 * Stock forms sign(x)*0.5, adds it to x and truncates the result. */
static int32_t ae_d26d0_round(float x)
{
    double rounded;
    if (!(x == x)) return 0;
    if (x == 0.0f) return 0;
    rounded = (double)x + (x > 0.0f ? 0.5 : -0.5);
    if (rounded >= 2147483647.0) return INT_MAX;
    if (rounded <= -2147483648.0) return INT_MIN;
    return (int32_t)rounded;
}

void fh8626_ae_runtime_set_slow_controller(struct fh8626_ae_runtime *ae,
                                            float scale, float divisor,
                                            float derivative)
{
    if (!ae) return;
    ae->slow_coeff_scale = scale;
    ae->slow_coeff_divisor = divisor;
    ae->slow_coeff_derivative = derivative;
    ae->slow_coeff_valid = (divisor != 0.0f);
}

static int ae_stats_normalize(struct fh8626_ae_runtime *ae)
{
    unsigned i;
    if (!ae->get_stats) return -ENOSYS;
    if (ae->get_stats(ae->stats_opaque, ae->raw18)) return -EIO;
    for (i = 0; i < 9; ++i) {
        uint32_t den = ae->raw18[9 + i];
        uint32_t num = ae->raw18[i];
        if (!den) return -ERANGE;
        ae->stats9[i] = num / den;
        if (ae->stats9[i] > 0xffffu) return -ERANGE;
    }
    return 0;
}

static int ae_refresh_sensor(struct fh8626_ae_runtime *ae, int publish_ctx)
{
    uint32_t intt = 0, gain = 0;
    int rc;
    if (!ae || !ae->sensor) return -EINVAL;
    rc = fh_sensor_gc1054_get_intt(ae->sensor, &intt);
    if (rc) return rc;
    rc = fh_sensor_gc1054_get_gain(ae->sensor, &gain);
    if (rc) return rc;
    ae->current_intt = intt;
    ae->current_gain = gain;
    if (publish_ctx && ae->ctx) {
        wr16(ae->ctx, 0x5a, (uint16_t)intt);
        wr16(ae->ctx, 0x5e, (uint16_t)gain);
    }
    return 0;
}

int fh8626_ae_runtime_refresh_sensor(struct fh8626_ae_runtime *ae)
{
    return ae_refresh_sensor(ae, 1);
}

int fh8626_ae_runtime_refresh_sensor_passive(struct fh8626_ae_runtime *ae)
{
    return ae_refresh_sensor(ae, 0);
}

static void ae_runtime_init_common(struct fh8626_ae_runtime *ae, uint8_t *ctx,
                                   volatile uint32_t *isp_mmio,
                                   struct fh_sensor_gc1054 *sensor,
                                   fh_ae_stats_fn get_stats, void *stats_opaque,
                                   fh_ae_timing_fn get_timing, void *timing_opaque,
                                   fh_ae_stage_fn stage, fh_ae_flush_fn flush, void *commit_opaque,
                                   int refresh_sensor)
{
    uint32_t base_vts = 0;
    if (!ae) return;
    memset(ae, 0, sizeof(*ae));
    ae->ctx = ctx;
    ae->isp_mmio = isp_mmio;
    ae->sensor = sensor;
    ae->get_stats = get_stats;
    ae->stats_opaque = stats_opaque;
    ae->get_timing = get_timing;
    ae->timing_opaque = timing_opaque;
    ae->stage = stage;
    ae->flush = flush;
    ae->commit_opaque = commit_opaque;
    ae->gate.state = 1;
    ae->last_factor_q12 = 4096;
    ae->q8_aux = 256u;
    ae->q8_alt = 256u;
    /* Bounded reset policy: six64/counter0 from three TARGET_LIVE stock
     * transition captures imported by ApplySpecialSeedEvidence. This is
     * captured-state restoration, not a proven static process-start seed.
     * Profile reload does not recreate this object or reset these queues. */
    for (unsigned i = 0; i < 6u; ++i) ae->special_queue[i] = 64u;
    /* Explicit bounded captured reset, TARGET_LIVE_POSITIVE_RESET overlay:
     * dwell0/delayed gain64, not a claim of static startup parity. */
    ae->positive_delayed_gain = 64u;
    ae->positive_delayed_gain_valid = 1;
    if (ctx) base_vts = rd16(ctx, 0x1a);
    ae->timing_multiplier = base_vts ? ((uint32_t)rd16(ctx, 0x5a) + 4u + base_vts - 1u) / base_vts : 1u;
    if (ae->timing_multiplier < 1u) ae->timing_multiplier = 1u;
    ae->initial_timing_multiplier = ae->timing_multiplier;
    if (refresh_sensor && !fh8626_ae_runtime_refresh_sensor(ae)) {
        ae->initial_intt = ae->current_intt;
        ae->initial_gain = ae->current_gain;
        ae->rollback_valid = 1;
        ae->initialized = 1;
    }
}

void fh8626_ae_runtime_init(struct fh8626_ae_runtime *ae, uint8_t *ctx,
                            volatile uint32_t *isp_mmio,
                            struct fh_sensor_gc1054 *sensor,
                            fh_ae_stats_fn get_stats, void *stats_opaque,
                            fh_ae_timing_fn get_timing, void *timing_opaque,
                            fh_ae_stage_fn stage, fh_ae_flush_fn flush, void *commit_opaque)
{
    ae_runtime_init_common(ae,ctx,isp_mmio,sensor,get_stats,stats_opaque,get_timing,timing_opaque,
                           stage,flush,commit_opaque,1);
}

void fh8626_ae_runtime_init_passive(struct fh8626_ae_runtime *ae, uint8_t *ctx,
                                    volatile uint32_t *isp_mmio,
                                    struct fh_sensor_gc1054 *sensor,
                                    fh_ae_stats_fn get_stats, void *stats_opaque,
                                    fh_ae_timing_fn get_timing, void *timing_opaque)
{
    ae_runtime_init_common(ae,ctx,isp_mmio,sensor,get_stats,stats_opaque,get_timing,timing_opaque,
                           NULL,NULL,NULL,0);
}

int fh8626_ae_runtime_enable_observe(struct fh8626_ae_runtime *ae, int enable)
{
    if (!ae) return -EINVAL;
    ae->observe_enabled = !!enable;
    if (!enable) ae->commit_enabled = 0;
    return 0;
}

int fh8626_ae_runtime_enable_commit(struct fh8626_ae_runtime *ae, int enable)
{
    if (!ae) return -EINVAL;
    if (!enable) {
        ae->commit_enabled = 0;
        return 0;
    }
    if (!ae->ctx) {
        ae->commit_enabled = 0;
        return -EOPNOTSUPP;
    }
    /* Signed selector is dispatched per tick: C7E04/C8134 or C7EB0/C883C. */
    ae->commit_enabled = 1;
    return 0;
}

static uint32_t clamp_u32(uint32_t v, uint32_t lo, uint32_t hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static int ae_isp_gain_write(struct fh8626_ae_runtime *ae, uint32_t value,
                              unsigned shift)
{
    uint32_t packed, active;
    if (!ae || !ae->ctx || !ae->isp_mmio) return -EINVAL;
    memcpy(&active, ae->ctx + 0xa70u, sizeof(active));
    if (!active) return -EAGAIN;
    /* C58F8 (pair2) shifts 3/19; C598C (pair3) shifts 1/17. */
    packed = ((value << shift) & 0x1fffu) |
             ((value << (shift + 16u)) & 0x1fff0000u);
    ae->isp_mmio[0x168u / 4u] = packed;
    ae->isp_mmio[0x16cu / 4u] = packed;
    return 0;
}

static int ae_apply_pair(struct fh8626_ae_runtime *ae, unsigned pair, uint32_t value)
{
    if (ae->stage) return ae->stage(ae->commit_opaque, pair, value);
    if (pair == 2u) return ae_isp_gain_write(ae, value, 3u);
    if (pair == 3u) return ae_isp_gain_write(ae, value, 1u);
    if (pair == 0u) return fh_sensor_gc1054_set_intt(ae->sensor, value);
    if (pair == 1u) return fh_sensor_gc1054_set_gain(ae->sensor, value);
    if (pair == 4u) return fh_sensor_gc1054_set_vts_multiplier(ae->sensor, value);
    return -ENOSYS;
}

static int ae_stage(struct fh8626_ae_runtime *ae, unsigned pair, uint32_t value)
{
    if (!ae || pair >= 5u) return -EINVAL;
    ae->queue[pair].value = value;
    ae->queue[pair].dirty = 1u;
    return 0;
}

int fh8626_ae_runtime_seed_special(struct fh8626_ae_runtime *ae,
                                   const uint32_t words[6], uint32_t counter)
{
    if (!ae || !words) return -EINVAL;
    memcpy(ae->special_queue, words, 6u * sizeof(*words));
    /* The seventh word is overwritten before any read in the spill branch.
     * Keep it isolated instead of corrupting the vendor module-head pointer. */
    ae->special_queue[6] = 0u;
    ae->special_counter = counter;
    return 0;
}

int fh8626_ae_runtime_special_step(struct fh8626_ae_runtime *ae)
{
    unsigned index, base, sensor_branch, multiple, i;
    uint32_t head, new_value, gain, isp_gain, intt;
    int first = 0, rc;
    int64_t signed_counter;
    if (!ae || !ae->ctx) return -EINVAL;
    if (!(ae->ctx[0x2c] & 0x10u)) return -EINVAL;
    if (!ae->observe_enabled || !ae->commit_enabled) return 0;
    sensor_branch = (ae->ctx[0x2c] & 0x20u) != 0u;
    if (!sensor_branch && !(ae->ctx[0x2d] & 1u)) {
        ++ae->special_counter;
        return 0;
    }
    signed_counter = ae->special_counter < 0x80000000u
        ? (int64_t)ae->special_counter : (int64_t)ae->special_counter - 0x100000000LL;
    multiple = signed_counter % 5 == 0;
    index = sensor_branch ? ae->ctx[0x2c] >> 6 : (ae->ctx[0x2d] >> 1) & 3u;
    base = sensor_branch ? 0u : 3u;
    new_value = sensor_branch ? (multiple ? 128u : 64u) : (multiple ? 64u : 128u);
    ae->special_queue[base + index] = new_value;
    head = ae->special_queue[base]; /* insertion precedes head read */
    for (i = 0; i < index; ++i)
        ae->special_queue[base + i] = ae->special_queue[base + i + 1u];
    gain = sensor_branch ? head : 64u;
    isp_gain = sensor_branch ? 64u : head;
    intt = sensor_branch ? (multiple ? 200u : 400u) : (multiple ? 400u : 200u);
    /* Exact C9240 immediate order; do not use normal deferred queue/flush. */
    rc = ae_apply_pair(ae, 1u, gain); if (rc) first = rc;
    rc = ae_apply_pair(ae, 2u, isp_gain); if (rc && !first) first = rc;
    rc = ae_apply_pair(ae, 0u, intt); if (rc && !first) first = rc;
    ++ae->special_counter;
    return first; /* stock ignores errors; owner records a nonfatal diagnostic */
}

/* C6AC8: commit the five dirty pairs in index order, then clear every dirty
 * record. Stock ignores callback results; production preserves the first
 * error while still completing and clearing the transaction. */
static int ae_flush(struct fh8626_ae_runtime *ae)
{
    int first = 0;
    unsigned i;
    if (!ae) return -EINVAL;
    for (i = 0u; i < 5u; ++i) {
        int rc;
        if (ae->queue[i].dirty != 1u) continue;
        rc = ae_apply_pair(ae, i, ae->queue[i].value);
        /* C6B74..C6B7C publishes pair4 even if its callback reports failure. */
        if (i == 4u && ae->published_timing_slot)
            *ae->published_timing_slot = ae->queue[i].value;
        if (rc && !first) first = rc;
    }
    if (ae->flush) {
        int rc = ae->flush(ae->commit_opaque);
        if (rc && !first) first = rc;
    }
    /* C6B40..C6B50 clear offsets 4,12,20,28,36 only. Values persist. */
    for (i = 0u; i < 5u; ++i) ae->queue[i].dirty = 0u;
    return first;
}

static int ae_apply_c90d4_bounds(struct fh8626_ae_runtime *ae)
{
    struct fh_ae_c90_state s;
    struct fh_ae_queue_pair q[5];
    uint16_t frame_limit, aux_max, gain_max;

    if (!ae || !ae->ctx) return -EINVAL;
    if (!ae->commit_enabled) return 0;
    frame_limit = rd16(ae->ctx, 0x1a);
    aux_max = rd16(ae->ctx, 0x34);
    gain_max = rd16(ae->ctx, 0x36);

    memset(&s, 0, sizeof(s));
    memset(q, 0, sizeof(q));
    s.intt = (uint16_t)ae->current_intt;
    s.aux = rd16(ae->ctx, 0x5c);
    s.gain = (uint16_t)ae->current_gain;
    s.q8_aux = ae->q8_aux;
    s.timing_multiplier = ae->timing_multiplier;
    fh_c90d4_bounds_stage(&s, q, (int8_t)ae->ctx[0x2f], aux_max, gain_max,
                          frame_limit, ae->ctx[0x3f]);

    ae->current_intt = s.intt;
    ae->current_gain = s.gain;
    ae->q8_aux = s.q8_aux;
    if (s.timing_multiplier) ae->timing_multiplier = s.timing_multiplier;
    wr16(ae->ctx, 0x5a, s.intt);
    wr16(ae->ctx, 0x5c, s.aux);
    wr16(ae->ctx, 0x5e, s.gain);

    for (unsigned i = 0; i < 5u; ++i)
        if (q[i].dirty) (void)ae_stage(ae, i, q[i].value);
    return 0;
}

static uint32_t ae_selected_q8(const struct fh8626_ae_runtime *ae)
{
    if ((ae->ctx[0x2c] & 0x02u) && (ae->ctx[0x3a] & 0x40u)) return ae->q8_alt;
    return ae->q8_aux;
}

static int ae_action4_sensor_gain(struct fh8626_ae_runtime *ae, float factor,
                                  uint32_t lo_gain, uint32_t hi_gain)
{
    uint32_t q8, candidate, actual, corr;
    int32_t scaled;
    int rc;
    if (!ae || !ae->ctx || !ae->sensor) return -EINVAL;
    q8 = ae->ctx[0x3a] > 0x7fu ? ae->q8_alt : ae->q8_aux;
    scaled = ae_positive_trunc((float)(ae->current_gain * q8) * factor);
    if (scaled > (int32_t)(hi_gain << 8)) scaled = (int32_t)(hi_gain << 8);
    if (scaled < (int32_t)(lo_gain << 8)) scaled = (int32_t)(lo_gain << 8);
    candidate = (uint32_t)scaled >> 8;
    if (candidate > hi_gain) candidate=hi_gain;
    if (candidate < lo_gain) candidate=lo_gain;
    /* C8EB8/C8EBC stage gain BEFORE get_gain. There is no immediate setter. */
    (void)ae_stage(ae,1u,candidate);
    ae->proposed_gain=candidate;
    actual=candidate;
    rc=fh_sensor_gc1054_get_gain(ae->sensor,&actual);
    if (ae->current_intt >= (uint32_t)rd16(ae->ctx,0x1a)-4u) return rc;
    if (!actual || actual > INT32_MAX ||
        (uint32_t)scaled > INT32_MAX-(actual>>1)) return rc?rc:-ERANGE;
    corr=(uint32_t)((scaled+(int32_t)(actual>>1))/(int32_t)actual);
    corr=clamp_u32(corr,256u,512u);
    ae->q8_aux=corr;
    (void)ae_stage(ae,3u,ae->ctx[0x3a]>0x7fu?ae->q8_alt:corr);
    return rc;
}

static int ae_antiflicker_quantum(struct fh8626_ae_runtime *ae, uint32_t *quantum)
{
    uint32_t timing[4], selector, period, base_vts;
    uint32_t product;
    int rc;
    if (!ae || !ae->ctx || !quantum || !ae->get_timing) return -ENOSYS;
    rc = ae->get_timing(ae->timing_opaque, timing);
    if (rc) return rc;
    selector = ae->ctx[0xa4c];
    period = selector ? (500000u / selector) : 500000u;
    base_vts = rd16(ae->ctx, 0x1a);
    if (!base_vts) return -ERANGE;
    /* C722C/C7230 are two MULs, not a widened three-factor product.
     * C7234/C7238: UMULL by 0x431BDE83, high32 >>18 (unsigned /1000000).
     * Retain ARM wrap semantics; unsupported timing ranges are not evidence
     * that silently widening this stock computation would be equivalent. */
    product = timing[2] * base_vts * period;
    *quantum = (uint32_t)(((uint64_t)product * 0x431bde83u) >> 50);
    if (*quantum < 1u) *quantum = 1u;
    ae->anti_flicker_quantum = *quantum;
    return 0;
}

int fh8626_ae_runtime_c72a0_antiflick(struct fh8626_ae_runtime *ae, float factor, uint32_t *new_intt)
{
    uint32_t quantum, min_intt, max_intt, max_quant, q8, candidate, corr;
    int32_t scaled;
    int rc;
    if (!ae || !ae->ctx) return -EINVAL;
    if (!(factor >= 0.0f) || factor > 3.402823466e38f) return -ERANGE;
    rc=ae_antiflicker_quantum(ae,&quantum);
    if (rc) return rc;
    min_intt=ae->ctx[0x3b]>>4; if (!min_intt) min_intt=1u;
    max_intt=rd16(ae->ctx,0x38);
    max_quant=max_intt/quantum; if (!max_quant) max_quant=1u;
    max_quant*=quantum;
    if (max_quant>0x7fffffu || quantum>0x7fffffu) return -ERANGE;
    q8=(ae->ctx[0x3a]&0x40u)?ae->q8_alt:ae->q8_aux;
    /* C7304: ARM low32 product BEFORE unsigned->float conversion. */
    scaled=ae_positive_trunc((float)((uint32_t)rd16(ae->ctx,0x5a)*q8)*factor);
    if (scaled>(int32_t)(max_quant<<8)) scaled=(int32_t)(max_quant<<8);
    if (scaled<(int32_t)(min_intt<<8)) scaled=(int32_t)(min_intt<<8);
    /* Compare full Q8 product, not its already-truncated line count. */
    if (scaled<=(int32_t)(quantum<<8) && !(ae->ctx[0xa4f]&0x80u)) quantum=1u;
    candidate=((uint32_t)scaled>>8)/quantum;
    if (!candidate) candidate=1u;
    candidate*=quantum;
    corr=((uint32_t)scaled+(candidate>>1))/candidate;
    corr=clamp_u32(corr,256u,512u);
    ae->q8_aux=corr;
    /* C7390..C73A4 only stage pairs0/3. NO sensor callback/readback here. */
    (void)ae_stage(ae,0u,candidate);
    (void)ae_stage(ae,3u,(ae->ctx[0x3a]&0x40u)?ae->q8_alt:corr);
    ae->proposed_intt=candidate;
    if (new_intt) *new_intt=candidate;
    return 0;
}

static int ae_action5_isp_gain(struct fh8626_ae_runtime *ae, float factor)
{
    uint32_t upper, q8;
    float scaled;
    if (!ae || !ae->ctx) return -EINVAL;
    upper = (uint32_t)rd16(ae->ctx, 0x34) << 2;
    if (upper < 256u) upper = 256u;
    scaled = (float)ae->q8_aux * factor;
    {
        int32_t value=ae_positive_trunc(scaled);
        q8=value<256?256u:(uint32_t)value;
    }
    q8 = clamp_u32(q8, 256u, upper);
    ae->q8_aux = q8;
    return ae_stage(ae, 3u, q8);
}

static int ae_action7_timing(struct fh8626_ae_runtime *ae, float factor)
{
    uint32_t base, span, mult, value, observed;
    int32_t scaled;
    int rc=0;
    if (!ae || !ae->ctx || !ae->sensor) return -EINVAL;
    base=rd16(ae->ctx,0x1a);
    if (base<=5u) return -ERANGE;
    span=base*((uint32_t)ae->ctx[0x3f]+1u);
    observed=ae->derived_timing_multiplier;
    scaled=ae_positive_trunc((float)ae->current_intt*factor);
    if (scaled>(int32_t)(span-5u)) scaled=(int32_t)(span-5u);
    if (scaled<(int32_t)(base-5u)) scaled=(int32_t)(base-5u);
    value=(uint32_t)scaled;
    if (value/(base-4u)==0u) mult=1u;
    else {
        mult=value/base+1u;
        if (mult>span/base) mult=span/base;
        if (mult<2u) mult=2u;
    }
    if (mult!=observed) {
        rc=ae_apply_pair(ae,4u,mult); /* immediate VTS change */
        value=(mult<observed?mult:observed)*base;
        if (value<5u) return rc?rc:-ERANGE;
        (void)ae_stage(ae,0u,value-5u);
        (void)ae_stage(ae,4u,mult); /* C8CDC: negative family also requeues VTS */
    } else {
        value=observed*base;
        if (value<5u) return -ERANGE;
        scaled=ae_positive_trunc((float)ae->current_intt*factor);
        if (scaled>(int32_t)(value-5u)) scaled=(int32_t)(value-5u);
        if (scaled<(int32_t)(base-5u)) scaled=(int32_t)(base-5u);
        (void)ae_stage(ae,0u,(uint32_t)scaled);
    }
    ae->proposed_intt=ae->queue[0].value;
    return rc;
}

static int ae_action6_slow_iris(struct fh8626_ae_runtime *ae)
{
    float a, b, c, d;
    int32_t n;
    if (!ae) return -EINVAL;
    if (!ae->slow_coeff_valid || ae->slow_coeff_divisor == 0.0f)
        return -EOPNOTSUPP;

    /* C883C code6 / ADJ_IRIS exact arithmetic order.  This branch mutates
     * only module+0x9C-equivalent slow state and does not touch GC1054. */
    a = (1.0f / ae->slow_coeff_divisor) * (float)ae->history_aggregate;
    b = a + (float)ae->error;
    c = (float)(int32_t)((uint32_t)ae->history[59] - (uint32_t)ae->history[58]) * ae->slow_coeff_derivative;
    d = (b + c) * ae->slow_coeff_scale;
    n = ae_d26d0_round(d);

    if (n <= 10) {
        if (ae->slow_iris == INT32_MIN) return -ERANGE;
        ae->slow_iris--;
    } else if (n >= 2690) {
        if (ae->slow_iris == INT32_MAX) return -ERANGE;
        ae->slow_iris++;
    }
    return 0;
}

/* Apollo C883C action-selection tree for the negative-selector family.
 * This is intentionally kept separate from actuator execution: selection is
 * pure state/profile logic, while C7058/C72A0/gain/timing helpers own writes.
 *
 * recovered locals:
 *   max_intt = C6D70-adjusted ctx+0x38
 *   gain_max = ctx+0x36
 *   gain_lo_ceiling = clamp(ctx+0x40,64,gain_max)
 *   gain_split = clamp(ctx+0x42 ? ctx+0x42 : gain_lo_ceiling,64,gain_max)
 *   min_intt = max(ctx+0x3b>>4,1)
 *   frame_span = ctx+0x1a * (ctx+0x3f+1)
 *   q8_aux = module+0x1f8
 *   previous action = module+0x200
 *
 * Codes 2/3/8 are AELOG names but are not selected by this C883C jump tree.
 */
static int ae_c883c_select_action(struct fh8626_ae_runtime *ae,
                                  uint32_t max_intt,
                                  uint32_t gain_max,
                                  uint32_t gain_lo_ceiling,
                                  uint32_t gain_split,
                                  uint32_t aux_upper,
                                  uint32_t frame_span,
                                  uint32_t derived_tm)
{
    uint32_t min_intt, gain, intt, q8;
    uint8_t flags;
    int brighter_than_target;

    if (!ae || !ae->ctx) return -EINVAL;
    flags = ae->ctx[0x2c];
    min_intt = (uint32_t)ae->ctx[0x3b] >> 4;
    if (min_intt < 1u) min_intt = 1u;
    intt = ae->current_intt;
    gain = ae->current_gain;
    q8 = ae->q8_aux;
    brighter_than_target = ae->measured > ae->target;

    if (!brighter_than_target) {
        /* C89EC..C8F5C: increase exposure in stock priority order. */
        if (flags & 0x04u) {
            if (ae->slow_iris <= 20) return 6;
            ae->slow_iris=20;
        }

        if ((flags & 0x01u) && intt < max_intt)
            return (flags & 0x08u) ? 9 : 1;

        if ((flags & 0x02u) && gain < gain_lo_ceiling)
            return 4;

        if ((flags & 0x01u) && ae->ctx[0x3f] != 0u &&
            frame_span > 5u && intt < frame_span - 5u)
            return 7;

        if ((flags & 0x02u) && gain < gain_max)
            return 4;

        if ((flags & 0x02u) && q8 < aux_upper)
            return 5;

        return 0;
    }

    /* C8944..C8FC8: reduce exposure.  C883C deliberately drains the local
     * Q8/sensor-gain stages before the final direct-integration reduction. */
    if ((flags & 0x02u) && q8 > 256u && gain >= gain_max)
        return 5;

    if ((flags & 0x02u) && gain > gain_split && intt >= max_intt)
        return 4;

    if (derived_tm > 1u || intt >= (uint32_t)rd16(ae->ctx, 0x1a) - 4u) {
        if (ae->ctx[0x3f] != 0u && (flags & 0x01u))
            return 7;
    }

    if ((flags & 0x02u) && intt >= max_intt && (q8 > 256u || gain > 64u))
        return 4;

    if ((flags & 0x01u) && intt >= min_intt)
        return (flags & 0x08u) ? 9 : 1;

    if (flags & 0x04u) {
        if (ae->slow_iris >= -20) return 6;
        ae->slow_iris=-20;
    }
    return 0;
}

static int ae_negative_dispatch(struct fh8626_ae_runtime *ae, float factor)
{
    uint32_t max_intt, gain_max, gain_hi, gain_mid, aux_upper, span, base, derived_tm;
    uint32_t previous_action, quantum;
    uint16_t effective;
    uint8_t flags;
    int action = 0;
    int rc = 0;
    if (!ae || !ae->ctx || !ae->sensor) return -EINVAL;
    if ((int8_t)ae->ctx[0x2f] >= 0) return -EOPNOTSUPP;
    if (!(factor >= 0.0f) || factor > 3.402823466e38f) return -ERANGE;
    flags = ae->ctx[0x2c];
    base = rd16(ae->ctx,0x1a);
    if (base<=5u) return -ERANGE;

    max_intt = fh_ae_effective_max_intt(rd16(ae->ctx,0x1a),rd16(ae->ctx,0x38));
    wr16(ae->ctx,0x38,(uint16_t)max_intt); /* C6D70 belongs inside actuator */
    /* With bit3 off quantum has no effect; do not require an unused provider
     * on standalone consumers. Owner supplies the real frontend provider. */
    if ((flags&8u) || ae->get_timing) {
        rc=ae_antiflicker_quantum(ae,&quantum);
        if (rc) return rc;
        if (flags&8u) { max_intt/=quantum; if (!max_intt) max_intt=1u; max_intt*=quantum; }
    }
    ae->current_intt=rd16(ae->ctx,0x5a);
    ae->current_gain=rd16(ae->ctx,0x5e);
    gain_max = rd16(ae->ctx, 0x36);
    gain_hi = rd16(ae->ctx, 0x40);
    if (gain_hi > gain_max) gain_hi = gain_max;
    if (gain_hi < 64u) gain_hi = 64u;
    gain_mid = rd16(ae->ctx, 0x42);
    if (!gain_mid) gain_mid = gain_hi;
    if (gain_mid > gain_max) gain_mid = gain_max;
    if (gain_mid < 64u) gain_mid = 64u;
    aux_upper = (uint32_t)rd16(ae->ctx, 0x34) << 2;
    if (aux_upper < 256u) aux_upper = 256u;
    base = rd16(ae->ctx, 0x1a);
    span = base * ((uint32_t)ae->ctx[0x3f] + 1u);
    rc=fh8626_ae_runtime_c6d9c_read_intt(ae,&effective);
    if (rc) return rc;
    derived_tm=(((effective+5u)/base)*base+(base>>1))/base;
    ae->derived_timing_multiplier=derived_tm;

    previous_action = ae->action_code;
    action = ae_c883c_select_action(ae, max_intt, gain_max, gain_hi,
                                    gain_mid, aux_upper, span, derived_tm);
    if (action < 0) return action;

    /* Restore delayed Q8 on every qualifying transition, including action0. */
    if ((uint32_t)action != previous_action &&
        ((previous_action==4u && (ae->ctx[0x3a]>>6)>=2u) ||
         ((previous_action==1u || previous_action==9u) && (ae->ctx[0x3a]&0x40u)))) {
        rc = ae_stage(ae, 3u, ae->q8_alt);
        if (rc) return rc;
    }

    ae->action_code = (uint32_t)action;
    if (ae->published_action_slot) *ae->published_action_slot=(uint32_t)action;
    if ((unsigned)action < 10u) ae->action_count[action]++;
    switch (action) {
    case 0:
        break;
    case 1:
        rc = fh8626_ae_runtime_c7058_intt(ae, factor, ae_selected_q8(ae), &ae->proposed_intt); break;
    case 4:
        if (ae->measured <= ae->target) {
            if (ae->current_gain < gain_hi)
                rc = ae_action4_sensor_gain(ae, factor, 64u, gain_hi);
            else
                rc = ae_action4_sensor_gain(ae, factor, gain_hi, gain_max);
        } else {
            if (ae->current_gain > gain_mid)
                rc = ae_action4_sensor_gain(ae, factor, gain_mid, gain_max);
            else
                rc = ae_action4_sensor_gain(ae, factor, 64u, gain_mid);
        }
        break;
    case 5:
        rc = ae_action5_isp_gain(ae, factor); break;
    case 6:
        rc = ae_action6_slow_iris(ae); break;
    case 7:
        rc = ae_action7_timing(ae, factor); break;
    case 9:
        rc = fh8626_ae_runtime_c72a0_antiflick(ae, factor, &ae->proposed_intt); break;
    default:
        return -EOPNOTSUPP;
    }
    return rc;
}

int fh8626_ae_runtime_c883c_day(struct fh8626_ae_runtime *ae, float factor)
{
    int rc;
    if (!ae || !ae->ctx || !ae->sensor) return -EINVAL;
    rc=ae_negative_dispatch(ae,factor);
    /* C949C must also flush pre-existing C90D4 bounds after provider failure.
     * Report the failure without leaking this epoch's queue into the next. */
    ae->q8_alt=ae->q8_aux; /* C949C C9648/C964C BEFORE C6AC8 */
    {
        int flushed=ae_flush(ae);
        if (!rc) rc=flushed;
    }
    if (!rc) {
        ae->commits++;
    }
    return rc;
}

/* Positive-selector C8134, separate from C883C: Q6 sensor/ISP gains,
 * its own five-epoch dwell, and observed frame-length provider C6D9C. */
int fh8626_ae_runtime_c6d9c_read_intt(struct fh8626_ae_runtime *ae, uint16_t *out)
{
    uint32_t num, den;
    uint16_t value;
    typedef void (*notify_fn)(uint32_t, uint16_t *, uint32_t);
    notify_fn notify = NULL;
    if (!ae || !out) return -EINVAL;
    if (ae->get_effective_intt)
        return ae->get_effective_intt(ae->effective_intt_opaque, out);
    if (!ae->isp_mmio) return -ENOSYS;
    num = ae->isp_mmio[0xa98u / 4u] + 1u;
    den = (uint16_t)(ae->isp_mmio[0xa94u / 4u] + 1u);
    if (!den) return -ERANGE;
    value = (uint16_t)(num / den);
    *(void **)(&notify) = fh_sensor_gc1054_cb(ae->sensor, 0x64u);
    if (notify) notify(1u, &value, 0u); /* stock ignores callback return */
    *out = value;
    return 0;
}

static int32_t ae_positive_trunc(float x)
{
    if (!(x == x)) return 0; /* ARM __aeabi_f2iz NaN */
    if (x >= 2147483648.0f) return INT32_MAX;
    if (x <= -2147483648.0f) return INT32_MIN;
    return (int32_t)x;
}

static int ae_positive_hold_gain(struct fh8626_ae_runtime *ae)
{
    if (ae->action_code == 1u && (ae->ctx[0x3a] & 0xc0u)) {
        if (!ae->positive_delayed_gain_valid) return -ENODATA;
        return ae_stage(ae, 1u, ae->positive_delayed_gain);
    }
    return 0;
}

int fh8626_ae_runtime_c8134_positive(struct fh8626_ae_runtime *ae, float factor)
{
    uint32_t base, max_intt, min_intt, gain, aux, gain_max, gain_hi, gain_mid;
    uint32_t intt, span, observed_span, tm, action = 0u, lo = 64u, hi;
    uint32_t flags, error_abs, threshold;
    uint16_t effective;
    int rc;
    if (!ae || !ae->ctx || !ae->sensor) return -EINVAL;
    if ((int8_t)ae->ctx[0x2f] < 0) return -EINVAL;
    if (!(factor >= 0.0f) || factor > 3.402823466e38f) return -ERANGE;
    base = rd16(ae->ctx, 0x1a);
    if (base <= 5u) return -ERANGE;
    max_intt = fh_ae_effective_max_intt((uint16_t)base, rd16(ae->ctx,0x38));
    wr16(ae->ctx,0x38,(uint16_t)max_intt);
    min_intt = ae->ctx[0x3b] >> 4;
    if (!min_intt) min_intt = 1u;
    intt = rd16(ae->ctx,0x5a);
    gain = rd16(ae->ctx,0x5e);
    aux = rd16(ae->ctx,0x5c);
    gain_max = rd16(ae->ctx,0x36);
    gain_hi = clamp_u32(rd16(ae->ctx,0x40),64u,gain_max);
    if (gain_hi < 64u) gain_hi = 64u; /* stock clamps upper then lower */
    gain_mid = rd16(ae->ctx,0x42);
    if (!gain_mid) gain_mid = gain_hi;
    if (gain_mid > gain_max) gain_mid = gain_max;
    if (gain_mid < 64u) gain_mid = 64u;
    hi = gain_max;
    rc = fh8626_ae_runtime_c6d9c_read_intt(ae,&effective);
    if (rc) return rc;
    observed_span = ((effective + 5u) / base) * base;
    tm = (observed_span + (base >> 1)) / base;
    ae->derived_timing_multiplier = tm; /* module+1E8, observed not commanded */
    span = base * ((uint32_t)ae->ctx[0x3f] + 1u);
    flags = ae->ctx[0x2c];
    error_abs = ae->measured > ae->target ? ae->measured-ae->target :
                                                       ae->target-ae->measured;
    threshold = (uint32_t)ae->ctx[0x32] * 32u;
    if (!ae->positive_dwell) {
        if (error_abs < threshold) ae->positive_dwell = 1u;
    } else if (ae->positive_dwell <= 4u && error_abs < threshold) {
        if (++ae->positive_dwell == 5u) ae->positive_dwell = 0u;
        return ae_positive_hold_gain(ae);
    } else ae->positive_dwell = 0u;

    if (ae->measured <= ae->target) {
        if (flags & 4u) {
            if (ae->slow_iris <= 20) { action=6; goto dispatch; }
            ae->slow_iris=20; /* C6E3C clamps only after exceeding boundary */
        }
        if ((flags & 1u) && intt < max_intt) action=1;
        else if ((flags & 2u) && gain < gain_hi) { action=4; hi=gain_hi; }
        else if ((flags & 1u) && ae->ctx[0x3f] && intt < span-5u) action=7;
        else if (gain < gain_max) {
            if (flags & 2u) { action=4; lo=gain_hi; }
        } else if ((flags & 2u) && aux < rd16(ae->ctx,0x34)) action=5;
    } else {
        if ((flags & 2u) && aux > 64u) { action=5; goto dispatch; }
        if ((flags & 2u) && gain > gain_mid && intt >= max_intt) {
            action=4; lo=gain_mid; goto dispatch;
        }
        if ((tm > 1u || intt >= base-4u) && ae->ctx[0x3f]) {
            if (flags & 1u) { action=7; goto dispatch; }
            /* C85F0: when long-exposure branch has integration disabled,
             * gain<=64 goes straight to iris, not integration selection. */
        }
        if ((flags & 2u) && gain > 64u && intt >= max_intt) {
            action=4; hi=gain_mid; goto dispatch;
        }
        if ((flags & 1u) && intt >= min_intt) { action=1; goto dispatch; }
        if (flags & 4u) {
            if (ae->slow_iris >= -20) action=6;
            else ae->slow_iris=-20; /* C6E14 */
        }
    }
dispatch:
    ae->action_code=action;
    ++ae->action_count[action];
    if (ae->published_action_slot) *ae->published_action_slot=action;
    switch (action) {
    case 0: return 0;
    case 1: return fh8626_ae_runtime_c6e64_intt_gain(ae,factor);
    case 4: {
        uint32_t log=0u, probe, mode=(ae->ctx[0x3a]>>3)&7u, step;
        int32_t delta=ae_positive_trunc((factor-1.0f)*(float)gain);
        int64_t candidate;
        for (probe=gain;probe>1u;probe>>=1) ++log;
        /* C83F8 ARM register LSL: negative exponent's low8>=32 => zero.
         * Unlike C6E64, this path does not clamp exponent at zero. */
        step=log<mode?0u:1u<<(log-mode);
        if (factor>1.0f) { if (delta<(int32_t)step) delta=(int32_t)step; }
        else if (delta>-(int32_t)step) delta=-(int32_t)step;
        candidate=(int64_t)gain+delta;
        if (candidate>(int64_t)hi) candidate=hi;
        if (candidate<(int64_t)lo) candidate=lo;
        return ae_stage(ae,1u,(uint32_t)candidate);
    }
    case 5: {
        int32_t v=ae_positive_trunc((float)aux*factor);
        if (v>(int32_t)rd16(ae->ctx,0x34)) v=rd16(ae->ctx,0x34);
        if (v<64) v=64;
        return ae_stage(ae,2u,(uint32_t)v); /* C58F8 Q6, not C598C Q8 */
    }
    case 6: {
        float integral, sum, derivative;
        double rounded;
        int32_t n;
        int64_t diff;
        if (!ae->slow_coeff_valid || ae->slow_coeff_divisor==0.0f)
            return -EOPNOTSUPP;
        integral=(1.0f/ae->slow_coeff_divisor)*(float)ae->history_aggregate;
        sum=integral+(float)error_abs; /* C86B4 is ABS error, not signed */
        diff=(int64_t)ae->history[59]-ae->history[58];
        /* explicit signed low32 for ARM SUB */
        diff=(uint32_t)diff;
        if (diff>=0x80000000LL) diff-=0x100000000LL;
        derivative=(float)diff*ae->slow_coeff_derivative;
        sum=(sum+derivative)*ae->slow_coeff_scale;
        if (!(sum==sum)) return -ERANGE;
        rounded=(double)sum+(sum>0.0f?0.5:sum<0.0f?-0.5:0.0);
        n=rounded>=2147483647.0?INT32_MAX:rounded<=-2147483648.0?INT32_MIN:(int32_t)rounded;
        if (n<=10) {
            if (ae->slow_iris==INT32_MIN) return -ERANGE;
            --ae->slow_iris; /* stock can reach -21, next clamp owns bound */
        } else if (n>=2690) {
            if (ae->slow_iris==INT32_MAX) return -ERANGE;
            ++ae->slow_iris;
        }
        return 0;
    }
    case 7: {
        int32_t scaled=ae_positive_trunc((float)intt*factor);
        uint32_t value, mult, limit=span-5u;
        if (scaled>(int32_t)limit) scaled=(int32_t)limit;
        if (scaled<(int32_t)(base-5u)) scaled=(int32_t)(base-5u);
        value=(uint32_t)scaled;
        if (value/(base-4u)==0u) mult=1u;
        else { mult=value/base+1u; if (mult>span/base) mult=span/base; if (mult<2u) mult=2u; }
        if (mult!=tm) {
            rc=ae_apply_pair(ae,4u,mult); /* immediate VTS, not a queued pair4 */
            value=(mult<tm?mult:tm)*base;
            if (value<5u) return rc?rc:-ERANGE;
            (void)ae_stage(ae,0u,value-5u);
            return rc;
        }
        scaled=ae_positive_trunc((float)intt*factor);
        if (observed_span<5u) return -ERANGE;
        if (scaled>(int32_t)(observed_span-5u)) scaled=(int32_t)(observed_span-5u);
        if (scaled<(int32_t)(base-5u)) scaled=(int32_t)(base-5u);
        return ae_stage(ae,0u,(uint32_t)scaled);
    }
    default: return -EINVAL;
    }
}

static int ae_dispatch_after_gate(struct fh8626_ae_runtime *ae, int gate_open)
{
    float factor;
    int rc, flushed;
    if ((int8_t)ae->ctx[0x2f] >= 0) {
        if (!ae->commit_enabled) return ae_flush(ae);
        if (!gate_open) rc=ae_positive_hold_gain(ae);
        else {
            factor=fh_ae_c7e04_factor(ae->measured,ae->target,ae->ctx[0x3c]);
            ae->last_factor_q12=factor>=1048575.0f?0xffffffffu:(uint32_t)(factor*4096.0f);
            rc=fh8626_ae_runtime_c8134_positive(ae,factor);
            ae->q8_alt=ae->q8_aux; /* C9648/C964C after either selector */
        }
        flushed=ae_flush(ae); /* C949C commits even after ignored callback errors */
        if (!rc) rc=flushed;
        if (!rc) ++ae->commits;
        return rc;
    }
    if (!gate_open) {
        uint32_t mode=ae->ctx[0x3a]>>6;
        if (ae->commit_enabled &&
            ((mode>=2u && ae->action_code==4u) ||
             ((mode&1u) && ae->action_code==((ae->ctx[0x2c]&8u)?9u:1u))))
            (void)ae_stage(ae,3u,ae->q8_alt);
        return ae_flush(ae);
    }
    factor=fh_ae_c7eb0_factor(ae->measured,ae->target,ae->ctx[0x3c]);
    ae->last_factor_q12=factor>=1048575.0f?0xffffffffu:(uint32_t)(factor*4096.0f);
    if (!ae->commit_enabled) return ae_flush(ae);
    return fh8626_ae_runtime_c883c_day(ae,factor);
}

static int ae_update_gate(struct fh8626_ae_runtime *ae, uint8_t t1, uint8_t t2, uint8_t lim)
{
    fh_ae_gate_begin(&ae->gate,ae->error,t1,t2,lim);
    /* C7CC0/C7CC8 pass signed error and current state BEFORE sign-cross and
     * accumulation overrides. Stock ignores callback return. */
    if (ae->gate_notify)
        ae->gate_notify(ae->gate_notify_opaque,ae->gate.state,ae->error);
    return fh_ae_gate_finish(&ae->gate,ae->history[59],ae->history[58]);
}

int fh8626_ae_runtime_tick(struct fh8626_ae_runtime *ae)
{
    int rc, gate_open;
    uint8_t t1, t2, lim;

    if (!ae || !ae->observe_enabled) return 0;
    if (!ae->ctx) return -EINVAL;
    if (ae->ctx[0x2c] & 0x10u) return fh8626_ae_runtime_special_step(ae);
    rc = ae_stats_normalize(ae);
    if (rc) { ae->stat_failures++; return rc; }

    if (!ae->initialized) {
        rc = fh8626_ae_runtime_refresh_sensor_passive(ae);
        if (rc) { ae->sensor_failures++; return rc; }
        ae->initial_intt = ae->current_intt;
        ae->initial_gain = ae->current_gain;
        ae->rollback_valid = 1;
        ae->initialized = 1;
    }

    if (ae->ctx && ae->ctx[0x3d] == 1)
        ae->measured = fh_ae_profile1_center_brightness(ae->stats9);
    else
        ae->measured = fh_ae_day_brightness(ae->stats9);
    ae->target = ae->ctx ? ((uint32_t)ae->ctx[0x30] << 4) : 1280u;
    ae->error = (int32_t)ae->measured - (int32_t)ae->target;

    ae->history_aggregate = fh_ae_history60_update(ae->history, ae->error);
    /* Current Ghidra C949C calls C6D04 at C95D8, then C90D4 at C95DC. */
    rc = ae_apply_c90d4_bounds(ae);
    if (rc) return rc;
    t1 = ae->ctx ? ae->ctx[0x31] : 3u;
    t2 = ae->ctx ? ae->ctx[0x32] : 5u;
    lim = ae->ctx ? ae->ctx[0x33] : 5u;
    gate_open = ae_update_gate(ae,t1,t2,lim);
    ae->updates++;

    ae->proposed_intt = ae->current_intt;
    ae->proposed_gain = ae->current_gain;
    return ae_dispatch_after_gate(ae,gate_open);
}

int fh8626_ae_runtime_step_metric(struct fh8626_ae_runtime *ae,
                                  uint32_t measured, uint32_t target)
{
    int rc, gate_open;
    uint8_t t1, t2, lim;

    if (!ae || !ae->observe_enabled || !ae->ctx) return 0;
    /* C7E04/C7EB0 explicitly handle measured0. Invalid populations are
     * rejected by C73F8 upstream, not by the brightness value itself. */
    if (measured > 4096u || target > 4096u) return -ERANGE;
    if (!ae->initialized) {
        rc = fh8626_ae_runtime_refresh_sensor_passive(ae);
        if (rc) { ae->sensor_failures++; return rc; }
        ae->initial_intt = ae->current_intt;
        ae->initial_gain = ae->current_gain;
        ae->rollback_valid = 1;
        ae->initialized = 1;
    }
    /* C90D4 consumes C73F8's epoch-aligned ctx5A/5E, not a second fresh
     * sensor sample. Fresh getters belong to actuation readback/status tail. */
    ae->current_intt = rd16(ae->ctx,0x5a);
    ae->current_gain = rd16(ae->ctx,0x5e);
    ae->measured = measured;
    ae->target = target;
    ae->error = (int32_t)measured - (int32_t)target;
    ae->history_aggregate = fh_ae_history60_update(ae->history, ae->error);
    rc = ae_apply_c90d4_bounds(ae);
    if (rc) return rc;
    t1 = ae->ctx[0x31]; t2 = ae->ctx[0x32]; lim = ae->ctx[0x33];
    gate_open = ae_update_gate(ae,t1,t2,lim);
    ae->updates++;
    ae->proposed_intt = ae->current_intt;
    ae->proposed_gain = ae->current_gain;
    return ae_dispatch_after_gate(ae,gate_open);
}

/* C6E64: positive-family Q6 product, immediate integration followed by
 * readback-based compensation staged in queue1. Do not substitute C7058's Q8
 * path or use requested integration as the divisor after sensor quantization.
 * Invalid profiles/providers fail explicitly; stock callback errors are ignored
 * for sequencing and retained here as diagnostics. No context readback publish
 * occurs inside the stock helper. */
int fh8626_ae_runtime_c6e64_intt_gain(struct fh8626_ae_runtime *ae, float factor)
{
    uint32_t intt, gain, basis, product, requested, actual, lower, upper;
    uint32_t value, step, shift = 0u, log = 0u, probe;
    int64_t signed_product;
    int32_t scaled, desired;
    float fscaled;
    int first, rc, delayed;
    if (!ae || !ae->ctx || !ae->sensor) return -EINVAL;
    if (!(factor >= 0.0f) || factor > 3.402823466e38f) return -ERANGE;
    intt = rd16(ae->ctx, 0x5a);
    gain = rd16(ae->ctx, 0x5e);
    lower = ae->ctx[0x3b] >> 4;
    if (lower < 1u) lower = 1u;
    upper = rd16(ae->ctx, 0x38);
    if (upper < lower) return -ERANGE;
    delayed = (ae->ctx[0x2c] & 2u) && (ae->ctx[0x3a] & 0xc0u);
    if (delayed && !ae->positive_delayed_gain_valid) return -ENODATA;
    basis = delayed ? ae->positive_delayed_gain : gain;
    product = intt * basis; /* ARM MUL low32 followed by signed i2f */
    signed_product = product < 0x80000000u ? (int64_t)product :
                       (int64_t)product - 0x100000000LL;
    fscaled = (float)signed_product * factor;
    if (fscaled >= 2147483648.0f) scaled = INT32_MAX;
    else if (fscaled <= -2147483648.0f) scaled = INT32_MIN;
    else scaled = (int32_t)fscaled;
    if (scaled > (int32_t)(upper * 64u)) scaled = (int32_t)(upper * 64u);
    if (scaled < (int32_t)(lower * 64u)) scaled = (int32_t)(lower * 64u);
    requested = (uint32_t)scaled >> 6;
    first = ae_apply_pair(ae, 0u, requested);
    ae->proposed_intt = requested;
    if (!(ae->ctx[0x2c] & 2u)) return first;
    actual = requested; /* stock local is initialized before both callbacks */
    rc = fh_sensor_gc1054_get_intt(ae->sensor, &actual);
    if (rc && !first) first = rc;
    if (!actual || actual > INT32_MAX) return first ? first : -ERANGE;
    /* Valid sensor domain keeps this signed stock addition non-overflowing. */
    if ((uint32_t)scaled > INT32_MAX - (actual >> 1))
        return first ? first : -ERANGE;
    desired = (scaled + (int32_t)(actual >> 1)) / (int32_t)actual;
    if (desired > 128) desired = 128;
    if (desired < 64) desired = 64;
    for (probe = gain; probe > 1u; probe >>= 1) ++log;
    value = (ae->ctx[0x3a] >> 3) & 7u;
    if (log > value) shift = log - value;
    step = 1u << shift;
    value = (uint32_t)desired;
    if (gain < value) {
        uint32_t n = (value - gain) / step;
        value = gain + (n ? n : 1u) * step;
    } else if (value < gain) {
        uint32_t n = (gain - value) / step;
        value = gain - (n ? n : 1u) * step;
    }
    ae->proposed_gain = value;
    (void)ae_stage(ae, 1u, delayed ? ae->positive_delayed_gain : value);
    if (delayed) ae->positive_delayed_gain = value;
    return first;
}

int fh8626_ae_runtime_c7058_intt(struct fh8626_ae_runtime *ae, float factor, uint32_t q8_state, uint32_t *new_intt)
{
    uint32_t current, lower, upper, requested, actual, corr;
    int32_t scaled;
    float product;
    int first, rc;
    if (!ae || !ae->ctx || !ae->sensor) return -EINVAL;
    if (!(factor >= 0.0f) || factor > 3.402823466e38f) return -ERANGE;
    current=rd16(ae->ctx,0x5a);
    lower=ae->ctx[0x3b]>>4; if (!lower) lower=1u;
    upper=rd16(ae->ctx,0x38);
    if (upper<lower) return -ERANGE;
    if ((ae->ctx[0x2c]&2u)&&(ae->ctx[0x3a]&0x40u))
        product=(float)(current*q8_state)*factor;
    else product=((float)current*factor)*(float)q8_state;
    scaled=ae_positive_trunc(product); /* signed f2iz, not unsigned cast */
    if (scaled>(int32_t)(upper<<8)) scaled=(int32_t)(upper<<8);
    if (scaled<(int32_t)(lower<<8)) scaled=(int32_t)(lower<<8);
    requested=(uint32_t)scaled>>8;
    first=ae_apply_pair(ae,0u,requested);
    ae->proposed_intt=requested;
    if (new_intt) *new_intt=requested;
    if (!(ae->ctx[0x2c]&2u)) return first;
    actual=requested;
    rc=fh_sensor_gc1054_get_intt(ae->sensor,&actual);
    if (rc&&!first) first=rc;
    if (!actual || actual>INT32_MAX ||
        (uint32_t)scaled>INT32_MAX-(actual>>1)) return first?first:-ERANGE;
    corr=(uint32_t)((scaled+(int32_t)(actual>>1))/(int32_t)actual);
    corr=clamp_u32(corr,256u,512u);
    ae->q8_aux=corr;
    (void)ae_stage(ae,3u,(ae->ctx[0x3a]&0x40u)?ae->q8_alt:corr);
    /* Stock retains ctx5A/5E snapshot; later C73F8 owns publication. */
    return first;
}

int fh8626_ae_runtime_rollback(struct fh8626_ae_runtime *ae)
{
    int rc0=0, rc1, rc2, rc3;
    if (!ae || !ae->rollback_valid || !ae->sensor) return -EINVAL;
    ae->commit_enabled = 0;
    if (ae->initial_timing_multiplier)
        rc0 = fh_sensor_gc1054_set_vts_multiplier(ae->sensor, ae->initial_timing_multiplier);
    rc1 = fh_sensor_gc1054_set_intt(ae->sensor, ae->initial_intt);
    rc2 = fh_sensor_gc1054_set_gain(ae->sensor, ae->initial_gain);
    rc3 = fh8626_ae_runtime_refresh_sensor(ae);
    if (rc0) return rc0;
    if (rc1) return rc1;
    if (rc2) return rc2;
    return rc3;
}
