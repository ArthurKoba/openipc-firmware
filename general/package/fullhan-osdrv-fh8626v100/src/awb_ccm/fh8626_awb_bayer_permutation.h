#ifndef FH8626_AWB_BAYER_PERMUTATION_H
#define FH8626_AWB_BAYER_PERMUTATION_H
#include <stdint.h>
static const uint8_t fh8626_stock_bayer_perm[4][4] = {
    {0,1,3,2},
    {1,0,2,3},
    {3,2,0,1},
    {2,3,1,0},
};
#endif
