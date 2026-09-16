#ifndef FH8626_STOCK_AE_STATE_REF_H
#define FH8626_STOCK_AE_STATE_REF_H
#include <stdint.h>
#include <stddef.h>

struct fh_ae_gate_state { int32_t accum; uint32_t state; uint32_t dwell; };
struct fh_gc1054_gain_regs { uint8_t write_main; uint8_t b6,b1,b2,page4_40; };
struct fh_gc1054_vts_regs { uint32_t desired_vts; uint16_t vblank; uint8_t reg07,reg08; };
/* D0460/D0630 six u32 words at record offsets +00,+04,+08,+0c,+10,+14. */
struct fh_d0630_record { uint32_t f0,f1,f2,f3,f4,f5; };
struct fh_ae_queue_pair { uint32_t value, dirty; };
struct fh_ae_c90_state {
    uint16_t intt, aux, gain;
    uint32_t q8_aux;
    uint32_t timing_multiplier;
};

uint32_t fh_isqrt_u32(uint32_t x);
uint32_t fh_ae_day_brightness(const uint32_t stats9[9]);
uint32_t fh_ae_profile1_center_brightness(const uint32_t stats9[9]);
float fh_ae_c7eb0_factor(uint32_t measured, uint32_t target, uint8_t smoothing);
float fh_ae_c7e04_factor(uint32_t measured, uint32_t target, uint8_t smoothing);
int32_t fh_ae_history60_update(int32_t hist60[60], int32_t err);
uint16_t fh_ae_effective_max_intt(uint16_t frame_limit, uint16_t effective);
void fh_c90d4_bounds_stage(struct fh_ae_c90_state *s, struct fh_ae_queue_pair q[5],
                           int8_t selector, uint16_t aux_max, uint16_t gain_max,
                           uint16_t base_vts, uint8_t frame_mult_ctl);
int fh_ae_gate_update(struct fh_ae_gate_state *s, int32_t err, int32_t newest, int32_t previous,
                      uint8_t thr_state1, uint8_t thr_state2, uint8_t dwell_limit);
/* C7C3C callback boundary: begin, optional callback(state,error), then finish.
 * Finish operands must be reread after callback if it can mutate history. */
void fh_ae_gate_begin(struct fh_ae_gate_state *s, int32_t err,
                       uint8_t thr_state1, uint8_t thr_state2, uint8_t dwell_limit);
int fh_ae_gate_finish(struct fh_ae_gate_state *s, int32_t newest, int32_t previous);
void fh_gc1054_intt_regs(uint32_t intt, uint8_t *reg03, uint8_t *reg04);
struct fh_gc1054_gain_regs fh_gc1054_gain_regs(uint32_t gain);
struct fh_gc1054_vts_regs fh_gc1054_vts_multiplier_regs(uint32_t multiplier, uint16_t base_vts, uint16_t active_height);
uint32_t fh_d27c4_log2_q8(uint32_t x);
void fh_d0460_init_records(struct fh_d0630_record r[4], uint32_t packed_dims);
void fh_d0630_history_core(struct fh_d0630_record r[4], uint32_t source0, uint32_t source2,
                           uint32_t current_p, uint32_t target_log_q8);
#endif
