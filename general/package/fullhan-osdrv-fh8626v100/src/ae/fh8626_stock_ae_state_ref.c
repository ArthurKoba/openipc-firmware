#include "fh8626_stock_ae_state_ref.h"
#include <limits.h>

static uint32_t umul_hi32(uint32_t a, uint32_t b) { return (uint32_t)(((uint64_t)a*b)>>32); }
static int32_t arm_i32(uint32_t x) {
    return x <= INT32_MAX ? (int32_t)x : -1 - (int32_t)(UINT32_MAX - x);
}

uint32_t fh_isqrt_u32(uint32_t x) {
    uint32_t res=0, bit=1u<<30;
    while (bit>x) bit>>=2;
    while (bit) { if (x>=res+bit) { x-=res+bit; res=(res>>1)+bit; } else res>>=1; bit>>=2; }
    return res;
}

uint32_t fh_ae_day_brightness(const uint32_t s[9]) {
    uint64_t sum=0; for (unsigned i=0;i<9;i++) sum+=s[i];
    uint32_t mean=(uint32_t)(sum/9u); if (mean>4096u) mean=4096u;
    uint32_t v=fh_isqrt_u32(mean<<12); return v>4096u?4096u:v;
}

/* C757C ctx+0x3D == 1: jump-table branch leaves only weight[4]=255; all other weights remain zero. */
uint32_t fh_ae_profile1_center_brightness(const uint32_t s[9]) {
    uint32_t mean=s[4] > 4096u ? 4096u : s[4];
    uint32_t v=fh_isqrt_u32(mean<<12);
    return v>4096u ? 4096u : v;
}

/* C7EB0 instruction-derived negative-selector correction factor.
 * The branch structure and constants are translated directly from Apollo
 * 0xC7EB0..0xC8130.  Native IEEE754 single operations reproduce the soft-float
 * arithmetic contract; the double comparisons are intentional because stock
 * promotes ratio to double for the 1.2/0.7 envelope checks. */
/* C7E04 positive-selector factor, current ARM soft-float ABI audit.
 * Separate from the C7EB0 envelope-limited negative-selector family. */
float fh_ae_c7e04_factor(uint32_t measured, uint32_t target, uint8_t smoothing)
{
    float den = (float)((uint32_t)smoothing + 1u);
    float ratio = (float)target / (float)(measured > 1u ? measured : 1u);
    if (ratio > 1.0f) den += 2.0f;
    return 1.0f - (1.0f - ratio) / den;
}

float fh_ae_c7eb0_factor(uint32_t measured, uint32_t target, uint8_t smoothing)
{
    float denom = (float)((uint32_t)smoothing + 1u);
    float m = (float)(measured > 1u ? measured : 1u);
    float ratio = (float)target / m;
    float factor;

    if (ratio > 1.0f) {
        denom = denom + 1.0f;
        if ((double)ratio <= 1.2) {
            factor = 1.0f - ((1.0f - ratio) / denom);
            if (factor >= 1.0078125f) factor = 1.0078125f;
            else if (factor < 1.0f) factor = 1.0f;
        } else {
            factor = 1.0f - ((1.0f - ratio) / (denom + 2.0f));
            if (factor >= 1.5f) factor = 1.5f;
            else if (factor < 1.0078125f) factor = 1.0078125f;
        }
    } else {
        if ((double)ratio < 0.7) {
            factor = 1.0f - ((1.0f - ratio) / denom);
            if (factor >= 0.9921875f) factor = 0.9921875f;
            else if ((double)factor < 0.6) factor = 0.6f;
        } else {
            factor = 1.0f - ((1.0f - ratio) / (denom + 2.0f));
            if (factor >= 1.0f) factor = 1.0f;
            else if (factor < 0.9921875f) factor = 0.9921875f;
        }
    }
    return factor;
}

/* C6D04 layout model: hist60[0] == module+0xE4 ... hist60[59] == module+0x1D0.
   Stock aggregate +0x1D4 is err + OLD hist60[0..58], while shift installs OLD hist60[1..59]. */
int32_t fh_ae_history60_update(int32_t h[60], int32_t err) {
    int64_t sum=err;
    for (unsigned i=0;i<59;i++) sum+=h[i];
    for (unsigned i=0;i<59;i++) h[i]=h[i+1];
    h[59]=err;
    return arm_i32((uint32_t)sum);
}

/* C6D70 mutates ctx+0x38 only when it has drifted to >= ctx+0x1A-4. */
uint16_t fh_ae_effective_max_intt(uint16_t frame_limit, uint16_t effective) {
    /* Keep the five-line safety margin without wrapping uint16_t for tiny
     * frame limits.  The stock arithmetic underflowed here. */
    if (frame_limit <= 5u) {
        if (effective >= frame_limit) effective = 0u;
    } else if (effective >= (uint16_t)(frame_limit - 4u)) {
        effective = (uint16_t)(frame_limit - 5u);
    }
    return effective;
}

/* Exact integer/state core of C90D4. q[0..4] are the shared queue pairs in C6AC8 order.
   The negative-selector q8 auxiliary clamp is conditional on gain reaching its ceiling. */
void fh_c90d4_bounds_stage(struct fh_ae_c90_state *s, struct fh_ae_queue_pair q[5],
                           int8_t selector, uint16_t aux_max, uint16_t gain_max,
                           uint16_t base_vts, uint8_t frame_mult_ctl) {
    uint32_t bound=(uint32_t)base_vts * ((uint32_t)frame_mult_ctl + 1u);
    uint16_t original_gain=s->gain;

    if (s->gain > gain_max) {
        s->gain=gain_max;
        q[1].value=gain_max; q[1].dirty=1;
    }

    if (selector >= 0) {
        if (s->aux > aux_max) {
            s->aux=aux_max;
            q[2].value=aux_max; q[2].dirty=1;
        }
    } else if (original_gain >= gain_max) {
        uint32_t lim=(uint32_t)aux_max << 2;
        if (s->q8_aux > lim) {
            s->q8_aux=lim;
            s->aux=aux_max;
            q[3].value=lim; q[3].dirty=1;
        }
    }

    if ((uint32_t)s->intt > bound) {
        /* Do not wrap the five-line reserve when the caller supplies a
         * malformed/tiny VTS, and never divide by a zero VTS. */
        uint32_t new_intt=(bound > 5u) ? bound-5u : 0u;
        uint32_t q1=(base_vts != 0u) ? new_intt/base_vts : 0u;
        uint32_t tm;
        if (q1==0) tm=1;
        else {
            uint32_t q2=(base_vts != 0u) ? bound/base_vts : 0u;
            tm=q1+1u;
            if (q2 < tm) tm=q2;
            if (tm < 2u) tm=2u;
        }
        s->intt=(uint16_t)new_intt;
        s->timing_multiplier=tm;
        q[0].value=new_intt; q[0].dirty=1;
        q[4].value=tm; q[4].dirty=1;
    }
}

/* ARM RSB wraps INT_MIN to itself; subsequent CMP is signed. */
static int32_t abs32(int32_t x) { return x<0 ? arm_i32(0u-(uint32_t)x) : x; }

/* C7C3C up to optional callback at C7CD0. No signed C overflow. */
void fh_ae_gate_begin(struct fh_ae_gate_state *s, int32_t err,
                       uint8_t t1, uint8_t t2, uint8_t lim) {
    s->accum = arm_i32((uint32_t)s->accum + (uint32_t)err);
    if (s->state==0) s->state=1;
    if (s->state==1) {
        if (abs32(err) >= ((int32_t)t1<<4)) s->dwell=0;
        else { s->dwell++; if (arm_i32(s->dwell) > lim) { s->state=2; s->dwell=0; } }
    } else if (s->state==2) {
        if (abs32(err) >= ((int32_t)t2<<4)) { s->dwell++; if (arm_i32(s->dwell) >= lim) { s->state=1; s->dwell=0; } }
        else s->dwell=0;
    } else { s->state=0; }
}
int fh_ae_gate_finish(struct fh_ae_gate_state *s, int32_t newest, int32_t previous) {
    /* C7CE4 MULS tests bit31 of LOW32, not a mathematical int64 product. */
    if (((uint32_t)newest*(uint32_t)previous)&0x80000000u) { s->state=2; s->dwell=0; }
    if (abs32(s->accum)>64000) { s->state=1; s->accum=0; s->dwell=0; return 1; }
    return s->state==1;
}
int fh_ae_gate_update(struct fh_ae_gate_state *s, int32_t err, int32_t newest, int32_t previous,
                      uint8_t t1, uint8_t t2, uint8_t lim) {
    fh_ae_gate_begin(s,err,t1,t2,lim);
    return fh_ae_gate_finish(s,newest,previous);
}

void fh_gc1054_intt_regs(uint32_t v, uint8_t *r03, uint8_t *r04) { *r03=(uint8_t)(v>>8); *r04=(uint8_t)v; }

/* GC1054 callback table +0x14 / lib+0x2064 -> helper 0x1E0C.
   The callback multiplies the active mode base VTS by an integer multiplier.
   0x1E0C then writes vblank = desired_VTS - active_height - 16 to regs 0x07/0x08.
   No semantic claim about which base VTS is the currently selected mode is made here. */
struct fh_gc1054_vts_regs fh_gc1054_vts_multiplier_regs(uint32_t multiplier, uint16_t base_vts, uint16_t active_height) {
    struct fh_gc1054_vts_regs r;
    r.desired_vts = multiplier * (uint32_t)base_vts;
    uint32_t vb = r.desired_vts - (uint32_t)active_height - 16u;
    r.vblank = (uint16_t)vb;
    r.reg07 = (uint8_t)(vb >> 8);
    r.reg08 = (uint8_t)vb;
    return r;
}

struct fh_gc1054_gain_regs fh_gc1054_gain_regs(uint32_t g) {
    struct fh_gc1054_gain_regs r={0,0,0,0, g<768?0:g<800?3:g<832?4:8};
    uint32_t x,h,y;
    if (g<=63) return r;
    r.write_main=1; r.b1=1;
    if (g<=90) { r.b6=0; r.b2=(uint8_t)((g<<2)&0xfc); }
    else if (g<=126) { r.b6=1; x=g<<6; h=umul_hi32(0x68168169u,x); y=h+((x-h)>>1); r.b2=(uint8_t)((y>>4)&0xfc); }
    else if (g<=181) { r.b6=2; x=g<<6; h=umul_hi32(0x02040811u,x); y=h+((x-h)>>1); r.b2=(uint8_t)((y>>4)&0xfc); }
    else if (g<=256) { r.b6=3; x=(g<<6)>>1; h=umul_hi32(0xb40b40b5u,x); r.b2=(uint8_t)((h>>4)&0xfc); }
    else if (g<=368) { r.b6=4; x=g<<6; h=umul_hi32(0xff00ff01u,x); r.b2=(uint8_t)((h>>6)&0xfc); }
    else if (g<=514) { r.b6=5; x=g<<6; h=umul_hi32(0xb19ab5c5u,x); r.b2=(uint8_t)((h>>6)&0xfc); }
    else if (g<=736) { r.b6=6; x=g<<6; h=umul_hi32(0x7f411e53u,x); r.b2=(uint8_t)((h>>6)&0xfc); }
    else if (g<=1030) { r.b6=7; x=g<<6; h=umul_hi32(0x63b0cda3u,x); y=h+((x-h)>>1); r.b2=(uint8_t)((y>>7)&0xfc); }
    else if (g<=1490) { r.b6=8; x=g<<6; h=umul_hi32(0x7f218557u,x); r.b2=(uint8_t)((h>>7)&0xfc); }
    else if (g<=2083) { r.b6=9; x=g<<6; h=umul_hi32(0x15fa298du,x); r.b2=(uint8_t)((h>>5)&0xfc); }
    else { r.b6=10; x=g<<6; h=umul_hi32(0xfb93e673u,x); r.b1=(uint8_t)(h>>17); r.b2=(uint8_t)(((h>>11)<<2)&0xfc); }
    return r;
}

static int32_t div8_trunc0(int32_t x) { return x / 8; }
static int32_t div3_trunc0(int32_t x) { return x / 3; }
static uint32_t clamp_u32(uint32_t v, uint32_t lo, uint32_t hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* D27C4: exact integer piecewise-linear approximation of log2(x) in Q8 for x>=1. */
uint32_t fh_d27c4_log2_q8(uint32_t x) {
    uint32_t t=x>>1, r1=0, r2, r3;
    if (t==0) return (x-1u)<<8; /* stock callers clamp x>=1; x=1 -> 0 */
    for (;;) {
        t >>= 1;
        r2 = r1 + 1u;
        if (t==0) break;
        r1 = r2;
    }
    if (r2 <= 7u) {
        r3 = r2 << 8;
        return r3 + ((x - (1u<<r2)) << (8u-r2));
    }
    return (r2<<8) + ((x - (1u<<r2)) >> (r1-7u));
}

void fh_d0460_init_records(struct fh_d0630_record r[4], uint32_t packed_dims) {
    uint32_t a=packed_dims & 0x7ffu;
    uint32_t b=(packed_dims>>16) & 0x7ffu;
    uint32_t p=a*b;
    uint32_t full=(p<<12)>>1;
    uint32_t eighth=(((p>>3)<<12)>>1);
    for (unsigned i=0;i<4;i++) {
        r[i].f0=0x0200u; r[i].f1=0x0e00u; r[i].f2=0x2700u; r[i].f3=0xbc00u;
        r[i].f4=(i<2)?full:eighth; r[i].f5=0;
    }
}

/* Exact history/filter core of D0630. Inputs source0/source2 and current_p are the values
   already produced/selected before the filter in stock. target_log_q8 is D27C4(...) output. */
void fh_d0630_history_core(struct fh_d0630_record r[4], uint32_t source0, uint32_t source2,
                           uint32_t current_p, uint32_t target_log_q8) {
    struct fh_d0630_record old0=r[0], old1=r[1], old2=r[2], old3=r[3];
    (void)old0;
    r[0]=old1;
    r[1]=old2;

    int32_t d0=(int32_t)(((old1.f0+old2.f0)>>1) - old3.f0);
    int32_t d1=(int32_t)(((old1.f1+old2.f1)>>1) - old3.f1);
    uint32_t den=current_p>>6;
    uint32_t t2=source0/den;
    int32_t d2=(int32_t)(t2-old3.f2);
    int32_t d3=(int32_t)(source2-old3.f3);
    r[3].f0=(uint32_t)((int32_t)old3.f0 + div8_trunc0(d0));
    r[3].f1=(uint32_t)((int32_t)old3.f1 + div3_trunc0(d1));
    r[3].f2=clamp_u32((uint32_t)((int32_t)old3.f2 + div8_trunc0(d2)),0x1100u,0xffffu);
    r[3].f3=clamp_u32((uint32_t)((int32_t)old3.f3 + div8_trunc0(d3)),0xa100u,0xffffu);

    uint64_t pair=((uint64_t)old3.f5<<32)|old3.f4;
    uint32_t q=(uint32_t)((pair<<3)/current_p);
    int32_t dq=(int32_t)(target_log_q8-q);
    q=(uint32_t)((int32_t)q + div3_trunc0(dq));
    r[3].f4=(q>>3)*current_p;
    r[3].f5=0;
}
