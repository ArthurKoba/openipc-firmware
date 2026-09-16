#ifndef FH8626_STOCK_AWB_MODE0_PIPELINE_H
#define FH8626_STOCK_AWB_MODE0_PIPELINE_H
#include <stdint.h>
#include "fh8626_stock_awb_mode0_ref.h"
#include "../diagnostics/fh8626_stock_ca4f4_diag.h"
struct fh_stock_awb_mode0 {
    struct fh_stock_awb_commit_state commit;
    int commit_valid;
    struct fh_stock_ca4f4_state mode1;
    uint32_t dispatch_epoch;
    int last_estimator_rc;
    int mode1_paused; /* Explicit owner override, not a stock context flag. */
    fh_stock_awb_sensor_gain_fn sensor_gain;
    fh_stock_awb_sensor_gain_fn sensor_query;
    void *sensor_gain_opaque;
};
struct fh_stock_awb_snapshot {
    struct fh_stock_awb_mode0 state;
    uint8_t context_a4_b3[16];
    uint8_t context_ccm[24]; /* CE670 working matrix at140..157 */
    uint8_t gate_bit,mode_bits;
    uint32_t registers[16]; /* Bayer, 4BC, 5D4, CCM and CA428 measurement */
    uint32_t profile_generation;
    uint64_t stream_generation;
    int valid;
};
void fh_stock_awb_snapshot_save(struct fh_stock_awb_snapshot *snapshot,
                                 const struct fh_stock_awb_mode0 *state,
                                 const uint8_t *ctx,const volatile uint32_t *regs,
                                 uint32_t profile,uint64_t generation);
/* Caller restores any external sensor side effects before this local commit. */
int fh_stock_awb_snapshot_restore(struct fh_stock_awb_snapshot *snapshot,
                                   struct fh_stock_awb_mode0 *state,uint8_t *ctx,
                                   volatile uint32_t *regs,uint32_t profile,uint64_t generation);
/* Complete CB5D4 dispatch. raw is nine accepted 16-byte records; it may be
 * NULL only for disabled/neutral branches which do not ingest statistics. */
int fh_stock_awb_dispatch(struct fh_stock_awb_mode0 *s,uint8_t *ctx,
                           volatile uint32_t *regs,const uint8_t *raw,
                           struct fh_stock_ca4f4_diag *diag);
void fh_stock_awb_publish_profile(uint8_t *ctx,volatile uint32_t *regs,unsigned selector);
/* CB4F0 ratios alone. Does not call C9F68 or publish 5D4. */
int fh_stock_awb_publish_ratios(const struct fh_stock_awb_commit_state *commit,uint8_t *ctx);
/* CB580 then CB7B0. Zero references fail explicitly instead of reproducing
 * the stock division helper's signal path. out/raw each span 9*16 bytes. */
int fh_stock_awb_prepare_stats(struct fh_stock_awb_commit_state *commit,
                                const uint8_t *ctx,const uint8_t *raw,uint8_t *out,
                                fh_stock_awb_sensor_gain_fn query,void *opaque);
/* CB6C4 halfword mirror only: preserve CAFC0 dwell/mode, sensor references,
 * recovery and dispatch history. triplet is already Bayer-selected by ISP. */
void fh_stock_awb_init_triplet(struct fh_stock_awb_mode0 *s,const uint32_t triplet[3]);
/* Bounded diagnostic takeover from existing MMIO, NOT stock initialization. */
void fh_stock_awb_commit_seed_bayer(struct fh_stock_awb_commit_state *s,
                                    uint32_t isp024,uint32_t isp084,uint32_t isp088,
                                    uint32_t r224,uint32_t r228);
typedef int (*fh_stock_awb_dependent_apply_fn)(void *opaque);
/* CB4F0 post-commit context publication followed by C9F68. */
int fh_stock_awb_publish_commit_state(const struct fh_stock_awb_commit_state *commit,
                                      uint8_t *ctx);
/* Compatibility wrapper: dispatches using ctx6C, with raw records at vmm+48. */
int fh_stock_awb_mode0_tick(struct fh_stock_awb_mode0 *s, uint8_t *ctx,
                            volatile uint32_t *regs, const uint8_t *isp_vmm);
/* Preserve stock CB970 ordering: AWB commit first, then dependent CCM apply. */
int fh_stock_awb_mode0_tick_then_apply(struct fh_stock_awb_mode0 *s, uint8_t *ctx,
                                       volatile uint32_t *regs, const uint8_t *isp_vmm,
                                       fh_stock_awb_dependent_apply_fn apply_dependent,
                                       void *opaque);
#endif
