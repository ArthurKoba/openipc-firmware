#ifndef FH8626_STOCK_D1724_REF_H
#define FH8626_STOCK_D1724_REF_H
#include <stdint.h>

struct fh8626_d1724_stat24 {
    uint32_t v0, v1, v2, v3, v4, v5;
};

struct fh8626_d1724_sums {
    uint32_t s0, s1, s2, s3, s4;
    uint32_t norm;
    uint32_t reciprocal;
};

void fh8626_d1648_copy_stats(struct fh8626_d1724_stat24 dst[32],
                             const struct fh8626_d1724_stat24 src[32]);
void fh8626_d1724_sum_stats(const struct fh8626_d1724_stat24 st[32],
                            struct fh8626_d1724_sums *out);
void fh8626_d16a4_apply_row(uint32_t isp_words[], const uint8_t ctx[],
                              const uint32_t rows[8][6]);
void fh8626_d1724_apply_default(uint32_t isp_words[], const uint8_t ctx[],
                                const struct fh8626_d1724_sums *s);
void fh8626_d1724_apply_range_tail(uint32_t isp_words[], const uint8_t ctx[]);
int32_t fh8626_d1724_interp_s13_q14(uint16_t a13, uint16_t b13, uint32_t w_q14);


struct fh8626_d1724_adaptive_debug {
    uint32_t target_norm_shifted;
    uint32_t tracked_norm;
    uint32_t blend_a;
    uint32_t blend_b;
    uint32_t desired_q14;
    uint32_t applied_q14;
};

void fh8626_d1724_apply_adaptive(uint32_t isp_words[], uint8_t ctx[],
                                 const struct fh8626_d1724_sums *s,
                                 struct fh8626_d1724_adaptive_debug *dbg);

#endif
