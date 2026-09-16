#ifndef FH8626_BOARD_DUALSENSOR_H
#define FH8626_BOARD_DUALSENSOR_H

#include <stdint.h>
#include "fh8626_gpio.h"

#define FH8626_LENS_UNKNOWN 0
#define FH8626_LENS_WIDE    1
#define FH8626_LENS_TELE    2

struct fh8626_dualsensor_board {
    struct fh8626_gpio gpio;
    int opened;
    int current_target;
    int gpio4;
    int gpio14;
    unsigned switches;
    unsigned verify_failures;
    unsigned rollback_attempts;
    unsigned rollback_failures;
    int last_rollback_error;
};

int fh8626_dualsensor_board_open(struct fh8626_dualsensor_board *b, const char *gpio_dev);
void fh8626_dualsensor_board_close(struct fh8626_dualsensor_board *b);
int fh8626_dualsensor_get(struct fh8626_dualsensor_board *b, uint32_t *raw);
int fh8626_dualsensor_select(struct fh8626_dualsensor_board *b, int target, uint32_t *before, uint32_t *after);
const char *fh8626_dualsensor_name(int target);

#endif
