#ifndef FH8626_STOCK_CA4F4_DIAG_H
#define FH8626_STOCK_CA4F4_DIAG_H
#include <stdint.h>

struct fh_stock_ca4f4_state { uint32_t recovery_count,recovery_active; };
typedef void (*fh_stock_ca4f4_profile_fn)(void *opaque,unsigned selector);

struct fh_stock_ca4f4_diag {
    uint32_t raw[9][4];
    uint32_t ratio_c0_c2[9];
    uint8_t valid[9];
    unsigned valid_count;
    uint32_t count_sum;
    uint32_t valid_count_sum;
    uint32_t ratio_min;
    uint32_t ratio_max;
    uint32_t ratio_sum_02;
    uint32_t ratio_sum_21;
    uint32_t ratio_sum_01;
    int16_t base[3];
    int16_t robust[3];
    int16_t mixed[3];
    int16_t final_gain[3];
    unsigned median_slot;
    unsigned weight;
    int gate_fallback;
    int stats_fallback;
    uint16_t fallback[3];
    uint32_t reg4bc_next;
    uint32_t ctx_ac_next;
    int publish_estimator; /* CA4F4 wrote 4BC/AC, not a fallback return. */
    int profile_selector; /* -1 means CA428 was not called. */
};

/* Production estimator with caller-owned module-static recovery state.
 * profile callback executes at the stock slot; NULL is read-only inspection. */
int fh_stock_ca4f4_compute(struct fh_stock_ca4f4_diag *d,const uint8_t *ctx,
                            const volatile uint32_t *regs,const uint8_t *stats,
                            const int16_t last_good[3],struct fh_stock_ca4f4_state *state,
                            fh_stock_ca4f4_profile_fn profile,void *opaque);

/* stats_src: nine CB7B0-normalized records, not unconditionally raw sensor stats. */
int fh_stock_ca4f4_diag_compute(struct fh_stock_ca4f4_diag *d,
                                const uint8_t *ctx,
                                const volatile uint32_t *regs,
                                const uint8_t *stats_src,
                                const int16_t last_good_gain[3]);
#endif
