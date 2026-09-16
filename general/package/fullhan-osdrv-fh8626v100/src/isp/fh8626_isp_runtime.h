#ifndef FH8626_ISP_RUNTIME_H
#define FH8626_ISP_RUNTIME_H

#include <stdint.h>
#include <stddef.h>
#include "fh8626_sensor_gc1054.h"

#define FH_ISP_MMIO_PHYS 0xE8400000u
#define FH_ISP_MMIO_SIZE 0x4000u
#define FH_ISP_PARAM_SIZE 0x0A5Cu
#define FH_ISP_SREG_PROFILE_SIZE 0x0A58u
/* Stock context continues well past the SREG payload.  Observed users include
 * +0xa70/+0xa8c, sensor callbacks at +0xc40 and API_ISP_Run fields at
 * +0x11a8/+0x11ac.  Keep headroom while reverse continues. */
#define FH_ISP_CTX_SIZE 0x1200u
#define FH_ISP_MMIO_PTR_OFF 0x0A5Cu
#define FH_ISP_SENSOR_CB_OFF 0x0C40u
#define FH_ISP_SENSOR_CB_SIZE 0x0068u

struct fh_isp_stock_ae_state {
    uint32_t slot[9];          /* C9C74 indices 0..8 */
    uint32_t dirty_mask;       /* vendor state +0x24 */
};

struct fh_isp_stock_stat_record {
    uint32_t value;
    uint32_t reserved[5];      /* C9898 record stride = 0x18 */
};

struct fh_isp_stock_d0460_record {
    uint32_t base_200;
    uint32_t base_e00;
    uint32_t base_2700;
    uint32_t base_bc00;
    uint32_t area_q11;
    uint32_t reserved_zero;
};

struct fh_isp_stock_d0460_state {
    struct fh_isp_stock_d0460_record rec[4];
};


struct fh_isp_stock_d1db0_coeffs {
    int32_t c16, c20, c24, c32, c36, c40;
};

struct fh_isp_stock_d1724_stat24 {
    uint32_t v0, v1, v2, v3, v4, v5;
};

struct fh_isp_stock_cb6c4_state {
    uint32_t owner_arg;
    uint32_t channel_value[3]; /* vendor +0xc4/+0xc8/+0xcc */
    uint32_t reserved_a4;
};

/* Public 2163FC/216938 ABI. Unknown byte meanings retain offset names.
 * reserved_4f is not read by Set or written by Get. No context overlay. */
struct fh_isp_ltm_attr {
    uint32_t mode, hw_enable;
    uint8_t ctrl_mode, sat_coeff_num, y_coeff_num;
    uint8_t value_0b, value_0c, value_0d, k2_offset, value_0f;
    uint8_t value_10, value_11, value_12;
    uint8_t table_a[12], table_b[12], table_c[12], table_d[12];
    uint8_t sat_coeff_map[12], reserved_4f;
};
_Static_assert(sizeof(struct fh_isp_ltm_attr)==0x50,"LTM public ABI size");
_Static_assert(offsetof(struct fh_isp_ltm_attr,table_a)==0x13,"LTM table offset");
_Static_assert(offsetof(struct fh_isp_ltm_attr,sat_coeff_map)==0x43,"LTM map offset");
_Static_assert(offsetof(struct fh_isp_ltm_attr,reserved_4f)==0x4f,"LTM padding offset");

/* AELOG notification adapter; invalid reports retain the original raw value.
 * NULL sink suppresses textual stock diagnostics, not validation/state. */
typedef void (*fh_isp_ae_log_fn)(void *opaque, unsigned slot, uint32_t value, int invalid);

/* Host binding for CB6C4's shared CAFC0 halfword mirror, not a sensor hook. */
typedef void (*fh_isp_awb_init_fn)(void *opaque,const uint32_t triplet[3]);

struct fh_isp_runtime {
    volatile uint32_t *mmio;
    size_t mmio_size;
    volatile uint8_t *isp_cfg;
    size_t isp_cfg_size;
    uint8_t ctx[FH_ISP_CTX_SIZE];
    struct fh_sensor_gc1054 *sensor;
    uint16_t width;
    uint16_t height;
    int params_dirty;
    unsigned stock_runtime_started;
    struct fh_isp_stock_ae_state ae_state;
    int last_ae_error; /* nonfatal measurement/AE status; AWB still runs */
    int last_awb_error; /* nonfatal CB5D4 status; do not freeze late ISP */
    int last_ae_log_error; /* C9898 invalid slot7/8; never abort AWB */
    int last_control_tail_error; /* failed status provider; independent slots advance */
    int last_stage_error; /* first rejected late stage, not tick failure */
    uint32_t last_stage_address; /* stock stage entry for that diagnostic */
    unsigned stage_error_count; /* all local rejections in this tick */
    fh_isp_ae_log_fn ae_log;
    void *ae_log_opaque;
    struct fh_isp_stock_stat_record ae_stats[7];
    uint32_t exposure_history[9]; /* C73F8 static +0x31a144 band */
    uint32_t gain_history[9];     /* C73F8 static +0x31a164 band */
    uint16_t total_gain_low12;    /* stock 0x31a13c initial value */
    uint32_t control_epoch;       /* accepted C949C/C73F8 publications */
    uint32_t gain_epoch;          /* epoch carried by ctx+0x60 */
    uint32_t source_stats_epoch; /* owner-accepted E2-equivalent generation */
    int32_t control_delta_history[60]; /* exact C6D04 +0xe4..+0x1d0 */
    int32_t control_delta;
    int32_t control_delta_aggregate;
    uint32_t control_metric_epoch;
    uint32_t ltm_group_sum[9];   /* C6C00 first nine-word snapshot */
    uint32_t ltm_group_count[9]; /* C6C00 second nine-word snapshot */
    uint32_t c73f8_group_value[9]; /* C73F8 quotient band consumed by C757C */
    uint32_t c73f8_invalid;      /* 0x31a294: C73F8 sets, C949C consumes/clears */
    uint32_t gain_ex;            /* C5B70: (ISP+0x168 >> 1) & 0x0fff */
    uint32_t c757c_group_value[9]; /* reduced values consumed by C757C */
    uint32_t c757c_weighted_mean;
    uint32_t c757c_metric_q12;
    uint32_t control_target_q12; /* module+0x80, C757C then C7894/C7AAC */
    uint32_t control_q8_aux;     /* owner AE module+0x1f8, sampled pre-epoch */
    uint32_t gamma_meta0;
    uint32_t gamma_meta1;
    int gamma_candidate_pending;
    uint32_t profile_generation; /* successful parameter loads */
    uint32_t temporal_generation;/* generation safe for temporal modules */
    unsigned nr3d_warmup_left;   /* valid publications required after reseed */
    uint32_t nr3d_warmup_epoch;  /* owner fence counts distinct accepted epochs */
    int nr3d_enabled;             /* runtime-owned feature gate */
    uint32_t ltm_summary_min_q8;  /* D0528 module publication */
    uint32_t ltm_summary_max_q8;
    uint32_t ltm_summary_calls;
    struct fh_isp_stock_d0460_state d0460_state;
    struct fh_isp_stock_cb6c4_state cb6c4_state;
    fh_isp_awb_init_fn awb_init;
    void *awb_init_opaque;
    struct fh_isp_stock_d1db0_coeffs d1db0_coeffs;
    int d1db0_coeffs_valid;
    const uint32_t (*d16a4_rows)[6]; /* stock GOT 0x3165D4, 8x24 bytes */
    struct fh_isp_stock_d1724_stat24 d1724_stats[32];
    int d1724_stats_valid;
};

typedef int (*fh_isp_runtime_awb_hook_fn)(void *opaque);
typedef int (*fh_isp_runtime_ae_hook_fn)(void *opaque);

void fh_isp_runtime_reset(struct fh_isp_runtime *rt);
int fh_isp_runtime_attach_mmio(struct fh_isp_runtime *rt, volatile void *mmio, size_t size);
int fh_isp_runtime_attach_isp_cfg(struct fh_isp_runtime *rt, volatile void *isp_cfg, size_t size);
int fh_isp_runtime_register_sensor(struct fh_isp_runtime *rt, struct fh_sensor_gc1054 *sensor);
int fh_isp_runtime_apply_vi_attr(struct fh_isp_runtime *rt, const void *vi_attr, size_t len);
int fh_isp_runtime_load_param(struct fh_isp_runtime *rt, const void *profile, size_t len);
int fh_isp_runtime_load_sreg_profile(struct fh_isp_runtime *rt, const void *container,
                                    size_t len, const char *profile_name);
int fh_isp_runtime_apply_geometry(struct fh_isp_runtime *rt, unsigned width, unsigned height);
int fh_isp_runtime_apply_c531c(struct fh_isp_runtime *rt);
int fh_isp_runtime_apply_format_bits(struct fh_isp_runtime *rt);
int fh_isp_runtime_finish_core_init(struct fh_isp_runtime *rt);
int fh_isp_runtime_init_proven_subset(struct fh_isp_runtime *rt, unsigned width, unsigned height);
int fh_isp_runtime_apply_c4998_static_defaults(struct fh_isp_runtime *rt);
int fh_isp_runtime_apply_c48cc_known(struct fh_isp_runtime *rt);
int fh_isp_runtime_apply_profile_luts(struct fh_isp_runtime *rt);
int fh_isp_runtime_apply_known_stock_init(struct fh_isp_runtime *rt, unsigned width, unsigned height);
int fh_isp_runtime_tick_proven_subset(struct fh_isp_runtime *rt);
int fh_isp_runtime_tick_with_awb_hook(struct fh_isp_runtime *rt,
                                      fh_isp_runtime_awb_hook_fn hook,
                                      void *opaque);
int fh_isp_runtime_tick_with_control_hooks(struct fh_isp_runtime *rt,
                                           fh_isp_runtime_ae_hook_fn ae_hook,
                                           void *ae_opaque,
                                           fh_isp_runtime_awb_hook_fn awb_hook,
                                           void *awb_opaque);
void fh_isp_runtime_set_nr3d_enabled(struct fh_isp_runtime *rt, int enabled);
int fh_isp_runtime_nr3d_ready(const struct fh_isp_runtime *rt);
void fh_isp_runtime_accept_stats_epoch(struct fh_isp_runtime *rt, uint32_t epoch);
int fh_isp_runtime_publish_control_metric(struct fh_isp_runtime *rt,
                                          uint32_t epoch,
                                          int32_t measured,
                                          int32_t target);
int fh_isp_runtime_apply_c757c_metric(struct fh_isp_runtime *rt,
                                      const uint32_t group_values[9]);
int fh_isp_runtime_apply_control_target(struct fh_isp_runtime *rt,
                                         uint32_t q8_aux, int alternate);
int fh_isp_runtime_snapshot_c6c00(struct fh_isp_runtime *rt);
int fh_isp_runtime_de6cc_scene_sample(const struct fh_isp_runtime *rt,
                                      uint32_t *sample);
int fh_isp_runtime_apply_ce430(struct fh_isp_runtime *rt);
int fh_isp_runtime_apply_d0e5c(struct fh_isp_runtime *rt);
int fh_isp_runtime_apply_ce764(struct fh_isp_runtime *rt);
int fh_isp_runtime_apply_cfd70(struct fh_isp_runtime *rt);
int fh_isp_runtime_apply_d1db0_lut(struct fh_isp_runtime *rt);
int fh_isp_runtime_set_d1db0_coeffs(struct fh_isp_runtime *rt, const struct fh_isp_stock_d1db0_coeffs *coeffs);
int fh_isp_runtime_set_d16a4_rows(struct fh_isp_runtime *rt, const uint32_t rows[8][6]);
int fh_isp_runtime_set_d1724_stats(struct fh_isp_runtime *rt, const struct fh_isp_stock_d1724_stat24 stats[32]);
int fh_isp_runtime_apply_d1724(struct fh_isp_runtime *rt);
int fh_isp_runtime_apply_d1258(struct fh_isp_runtime *rt);
int fh_isp_runtime_apply_d0238(struct fh_isp_runtime *rt);
int fh_isp_runtime_apply_cfbc4(struct fh_isp_runtime *rt);
int fh_isp_runtime_apply_d2074(struct fh_isp_runtime *rt);
int fh_isp_runtime_apply_ceacc(struct fh_isp_runtime *rt);
int fh_isp_runtime_apply_ce7d8(struct fh_isp_runtime *rt);
int fh_isp_runtime_apply_ced28(struct fh_isp_runtime *rt);
int fh_isp_runtime_apply_d0df4_control(struct fh_isp_runtime *rt);
/* Caller serializes with control ticks and supplies a separate attr object.
 * Set clamps the caller buffer in place; neither function touches MMIO,
 * dirty flags or ctx13 periodic gate. NULL attr: stock error -3002. */
int fh_isp_runtime_get_ltm_attr(const struct fh_isp_runtime *rt,struct fh_isp_ltm_attr *attr);
int fh_isp_runtime_set_ltm_attr(struct fh_isp_runtime *rt,struct fh_isp_ltm_attr *attr);
/* Explicit owner command adapter: Get/Set hardware bit and commit only024
 * bit18 immediately, including while periodic updates are paused. */
int fh_isp_runtime_set_ltm_enabled(struct fh_isp_runtime *rt,int enabled);
int fh_isp_runtime_apply_dynamic_ltm(struct fh_isp_runtime *rt);
int fh_isp_runtime_apply_d0b2c(struct fh_isp_runtime *rt);
int fh_isp_runtime_apply_cdd6c(struct fh_isp_runtime *rt);
int fh_isp_runtime_apply_d0fec(struct fh_isp_runtime *rt);
/* Owner opt-in fence, NOT a stock buffer-reset routine. Call only after a
 * successful kernel-off request; preserve all module-static AE histories. */
void fh_isp_runtime_prepare_nr3d_reenable(struct fh_isp_runtime *rt);
int fh_isp_runtime_apply_cecf0(struct fh_isp_runtime *rt);
int fh_isp_runtime_cb890_init_ae_state(struct fh_isp_runtime *rt);
int fh_isp_runtime_cb890_init_d0460(struct fh_isp_runtime *rt);
int fh_isp_runtime_cb890_init_cfd00(struct fh_isp_runtime *rt);
int fh_isp_runtime_cb890_init_cb6c4(struct fh_isp_runtime *rt, uint32_t owner_arg);
int fh_isp_runtime_c949c_set_slot(struct fh_isp_runtime *rt, unsigned index, uint32_t value);
int fh_isp_runtime_c9898_ingest_stat0(struct fh_isp_runtime *rt, uint32_t stat0);
int fh_isp_runtime_c949c_publish_tail(struct fh_isp_runtime *rt,
                                     uint32_t driver_mask, uint32_t gate_state);
int fh_isp_runtime_c949c_update_known(struct fh_isp_runtime *rt);
int fh_isp_runtime_apply_c73f8(struct fh_isp_runtime *rt);
int fh_isp_runtime_apply_c77dc(struct fh_isp_runtime *rt);

static inline uint8_t *fh_isp_runtime_param(struct fh_isp_runtime *rt) { return rt ? rt->ctx : (uint8_t *)0; }
static inline const uint8_t *fh_isp_runtime_param_const(const struct fh_isp_runtime *rt) { return rt ? rt->ctx : (const uint8_t *)0; }

#endif
