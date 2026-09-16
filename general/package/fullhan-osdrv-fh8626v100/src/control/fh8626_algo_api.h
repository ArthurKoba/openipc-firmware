#ifndef FH8626_ALGO_API_H
#define FH8626_ALGO_API_H
#include <stdint.h>
#include <stddef.h>
#define FH_ALGO_ABI_VERSION 1u
struct fh_algo_host {
    uint32_t abi_version;
    volatile uint32_t *regs;
    uint8_t *param;
    size_t param_size;
    void *sensor;
    int (*set_intt)(void *sensor, uint32_t intt);
    int (*set_gain)(void *sensor, uint32_t gain);
    int (*write_reg)(void *sensor, uint32_t reg, uint32_t value);
};
typedef int (*fh_algo_init_fn)(struct fh_algo_host *host);
typedef int (*fh_algo_tick_fn)(struct fh_algo_host *host, uint64_t frame_no);
typedef void (*fh_algo_fini_fn)(struct fh_algo_host *host);
#endif
