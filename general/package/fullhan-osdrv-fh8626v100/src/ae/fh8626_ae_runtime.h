#ifndef FH8626_AE_RUNTIME_H
#define FH8626_AE_RUNTIME_H

#include <stdint.h>
#include "fh8626_sensor_gc1054.h"
#include "fh8626_stock_ae_state_ref.h"

typedef int (*fh_ae_stats_fn)(void *opaque, uint32_t raw18[18]);
typedef int (*fh_ae_timing_fn)(void *opaque, uint32_t timing[4]);
typedef int (*fh_ae_stage_fn)(void *opaque, unsigned pair, uint32_t value);
typedef int (*fh_ae_flush_fn)(void *opaque);
typedef int (*fh_ae_effective_intt_fn)(void *opaque, uint16_t *value);
typedef void (*fh_ae_gate_notify_fn)(void *opaque, uint32_t state, int32_t error);

struct fh8626_ae_runtime {
    uint8_t *ctx;
    volatile uint32_t *isp_mmio;
    struct fh_sensor_gc1054 *sensor;
    fh_ae_stats_fn get_stats;
    void *stats_opaque;
    fh_ae_timing_fn get_timing;
    void *timing_opaque;
    fh_ae_stage_fn stage;
    fh_ae_flush_fn flush;
    void *commit_opaque;

    int observe_enabled;
    int commit_enabled;
    int initialized;
    int rollback_valid;

    uint32_t raw18[18];
    uint32_t stats9[9];
    uint32_t measured;
    uint32_t target;
    int32_t error;
    int32_t history[60];
    int32_t history_aggregate;
    struct fh_ae_gate_state gate;
    struct fh_ae_queue_pair queue[5];
    uint32_t *published_timing_slot; /* optional owner control slot4 binding */
    uint32_t special_queue[7]; /* two adjacent 3-word queues + isolated spill */
    uint32_t special_counter; /* ARM32 wrapping signed modulo-5 input */
    uint32_t positive_delayed_gain; /* module+1EC, not the negative Q8 state */
    int positive_delayed_gain_valid; /* explicit seed, never inferred from Q8 */
    uint32_t positive_dwell; /* module+1E4, distinct from C7C3C gate */
    uint32_t *published_action_slot; /* optional C9C74 slot8 owner binding */
    fh_ae_effective_intt_fn get_effective_intt;
    void *effective_intt_opaque;
    fh_ae_gate_notify_fn gate_notify; /* C984C registration; C9740 default NULL */
    void *gate_notify_opaque;

    uint32_t current_intt;
    uint32_t current_gain;
    uint32_t proposed_intt;
    uint32_t proposed_gain;
    uint32_t initial_intt;
    uint32_t initial_gain;
    uint32_t last_factor_q12;
    uint32_t q8_aux;
    uint32_t q8_alt;
    uint32_t timing_multiplier;
    uint32_t initial_timing_multiplier;
    uint32_t derived_timing_multiplier;
    uint32_t anti_flicker_quantum;
    int32_t slow_iris;
    float slow_coeff_scale;
    float slow_coeff_divisor;
    float slow_coeff_derivative;
    int slow_coeff_valid;
    uint32_t action_code;
    uint32_t action_count[10];
    uint32_t updates;
    uint32_t commits;
    uint32_t stat_failures;
    uint32_t sensor_failures;
};

void fh8626_ae_runtime_init(struct fh8626_ae_runtime *ae, uint8_t *ctx,
                            volatile uint32_t *isp_mmio,
                            struct fh_sensor_gc1054 *sensor,
                            fh_ae_stats_fn get_stats, void *stats_opaque,
                            fh_ae_timing_fn get_timing, void *timing_opaque,
                            fh_ae_stage_fn stage, fh_ae_flush_fn flush, void *commit_opaque);
void fh8626_ae_runtime_init_passive(struct fh8626_ae_runtime *ae, uint8_t *ctx,
                                    volatile uint32_t *isp_mmio,
                                    struct fh_sensor_gc1054 *sensor,
                                    fh_ae_stats_fn get_stats, void *stats_opaque,
                                    fh_ae_timing_fn get_timing, void *timing_opaque);
int fh8626_ae_runtime_enable_observe(struct fh8626_ae_runtime *ae, int enable);
int fh8626_ae_runtime_enable_commit(struct fh8626_ae_runtime *ae, int enable);
int fh8626_ae_runtime_tick(struct fh8626_ae_runtime *ae);
int fh8626_ae_runtime_special_step(struct fh8626_ae_runtime *ae);
int fh8626_ae_runtime_seed_special(struct fh8626_ae_runtime *ae,
                                   const uint32_t words[6], uint32_t counter);
int fh8626_ae_runtime_step_metric(struct fh8626_ae_runtime *ae,
                                  uint32_t measured, uint32_t target);
int fh8626_ae_runtime_rollback(struct fh8626_ae_runtime *ae);
int fh8626_ae_runtime_refresh_sensor(struct fh8626_ae_runtime *ae);
int fh8626_ae_runtime_refresh_sensor_passive(struct fh8626_ae_runtime *ae);
int fh8626_ae_runtime_c7058_intt(struct fh8626_ae_runtime *ae, float factor, uint32_t q8_state, uint32_t *new_intt);
int fh8626_ae_runtime_c6e64_intt_gain(struct fh8626_ae_runtime *ae, float factor);
int fh8626_ae_runtime_c6d9c_read_intt(struct fh8626_ae_runtime *ae, uint16_t *out);
int fh8626_ae_runtime_c8134_positive(struct fh8626_ae_runtime *ae, float factor);
int fh8626_ae_runtime_c72a0_antiflick(struct fh8626_ae_runtime *ae, float factor, uint32_t *new_intt);
int fh8626_ae_runtime_c883c_day(struct fh8626_ae_runtime *ae, float factor);
void fh8626_ae_runtime_set_slow_controller(struct fh8626_ae_runtime *ae, float scale, float divisor, float derivative);

#endif
