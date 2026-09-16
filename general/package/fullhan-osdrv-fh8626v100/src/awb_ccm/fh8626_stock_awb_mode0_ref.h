#ifndef FH8626_STOCK_AWB_MODE0_REF_H
#define FH8626_STOCK_AWB_MODE0_REF_H
#include <stdint.h>

struct fh_stock_awb_stat_rec { uint32_t c0,c1,c2,count; };
struct fh_stock_awb_commit_state {
    uint32_t ctr0,ctr1,ctr2,mode;
    int16_t cur0,cur1,cur2;
    /* Logical model, NOT a binary overlay of stock state. */
    uint32_t sensor_gain[3];
};
typedef void (*fh_stock_awb_sensor_gain_fn)(void *opaque,uint32_t gain[3]);

/* CA13C including persistent-gain fallback. Return 1 denotes a handled
 * fallback; -EDOM rejects stock divide-by-zero inputs without hardware writes. */
int fh_stock_ca13c(const struct fh_stock_awb_stat_rec rec[9],
                    uint32_t isp_4d8,uint32_t isp_4dc,int8_t ctx_dc,
                    uint32_t *isp_4bc,uint32_t *ctx_ac_word,
                    const int16_t last_good[3],int16_t out_gain[3]);
/* Shared literal normalization tail of CA13C and CA4F4. */
int fh_stock_awb_normalize_targets(const int16_t base[3],uint32_t isp_4d8,
                                   uint32_t isp_4dc,int8_t ctx_dc,
                                   uint32_t *isp_4bc,int16_t out_gain[3]);
/* Compatibility entry restricted to valid statistics (no fallback source). */
int fh_stock_ca13c_normal(const struct fh_stock_awb_stat_rec rec[9],
                          uint32_t isp_4d8, uint32_t isp_4dc,
                          int8_t ctx_dc, uint32_t *isp_4bc,
                          uint32_t *ctx_ac_word, int16_t out_gain[3]);

/* Full CAFC0 controls from current Ghidra. ctx spans at least 0x7f bytes;
 * optional sensor callback may change ctx6e/ctx7c..7e before final scaling.
 * MMIO inputs are a stable caller snapshot. NULL callback is valid stock. */
void fh_stock_cafc0(struct fh_stock_awb_commit_state *st,
                     const int16_t target[3],uint8_t *ctx,
                     uint32_t isp_024,uint32_t isp_084,uint32_t isp_088,
                     fh_stock_awb_sensor_gain_fn sensor_gain,void *opaque,
                     uint32_t *isp_224,uint32_t *isp_228);
/* Compatibility helper for ctx6c=1,ctx6d=0x0f,ctx6e=0 only. */
void fh_stock_cafc0_day(struct fh_stock_awb_commit_state *st,
                        const int16_t target[3], uint32_t isp_024,
                        uint32_t isp_084, uint32_t isp_088,
                        uint32_t *isp_224, uint32_t *isp_228);
#endif
