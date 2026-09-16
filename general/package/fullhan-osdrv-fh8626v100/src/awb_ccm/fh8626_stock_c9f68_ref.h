#ifndef FH8626_STOCK_C9F68_REF_H
#define FH8626_STOCK_C9F68_REF_H
#include <stdint.h>

/* Exact reference for C9DB0/C9F30/C9F68.
 * ctx is the stock ISP runtime context/profile base (>= 0xb3 bytes).
 */
uint32_t fh_stock_d27c4(uint32_t x);
uint32_t fh_stock_c9db0(uint32_t packed_xy);
uint32_t fh_stock_c9f30(uint32_t a, uint32_t b);
void fh_stock_c9f68(uint8_t *ctx);
#endif
