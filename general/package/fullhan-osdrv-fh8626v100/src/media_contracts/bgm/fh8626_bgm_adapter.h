#ifndef FH8626_BGM_ADAPTER_H
#define FH8626_BGM_ADAPTER_H

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include "fh8626_bgm_abi.h"

enum fh_bgm_state {
    FH_BGM_CLOSED = 0,
    FH_BGM_STARTING,
    FH_BGM_READY,
    FH_BGM_RUNNING,
    FH_BGM_STOPPING,
    FH_BGM_HELD,
    FH_BGM_POISONED,
    FH_BGM_RECOVERY_REQUIRED
};

enum fh_bgm_mode {
    FH_BGM_MODE_OFF = 0,
    FH_BGM_MODE_OBSERVE = 1,
    /* Stock media graph feeds BGM through MEDIA_BIND 5 -> 17. */
    FH_BGM_MODE_ENCODER_NATIVE = 2
};

struct fh_bgm_block {
    uint32_t phys;
    uint32_t bytes;
    uintptr_t user;
};

typedef int (*fh_bgm_alloc_fn)(void *opaque, uint32_t bytes, uint32_t align,
                               struct fh_bgm_block *out);
typedef int (*fh_bgm_free_fn)(void *opaque, struct fh_bgm_block *block);
typedef int (*fh_bgm_ioctl_fn)(void *opaque, unsigned long req, void *arg,
                               uint32_t *raw_status);
/* final_release is the proven process/restart boundary: close /dev/bgm first,
 * then release the dedicated BGM VMM allocation. It must clear block only on
 * confirmed success. reopen creates a fresh BGM/VMM owner epoch. */
typedef int (*fh_bgm_final_release_fn)(void *opaque, struct fh_bgm_block *block);
typedef int (*fh_bgm_reopen_fn)(void *opaque);
/* Backend reports retained/unknown ownership (for example VMM ALLOC succeeded
 * but REMAP failed).  This prevents an allocation failure from being
 * misclassified as a clean CLOSED state. */
typedef int (*fh_bgm_backend_needs_recovery_fn)(void *opaque);

struct fh_bgm_ops {
    fh_bgm_alloc_fn alloc;
    fh_bgm_free_fn free;
    fh_bgm_ioctl_fn ioctl;
    fh_bgm_final_release_fn final_release;
    fh_bgm_reopen_fn reopen;
    fh_bgm_backend_needs_recovery_fn backend_needs_recovery;
    void *opaque;
};

struct fh_bgm_frame {
    uint32_t width, height, y_phys;
    uint64_t pts;
};
struct fh_bgm_result {
    struct fh_bgm_sw_result raw;
    uint64_t pts;
    uint64_t generation;
};

struct fh_bgm_adapter_status {
    enum fh_bgm_state state;
    enum fh_bgm_mode mode;
    uint32_t last_driver_status;
    uint32_t in_flight;
    uint64_t generation;
    uint64_t submitted, completed, rejected, stale, failures, recoveries;
    int memory_allocated;
    int mem_initialized;
    int enabled;
    int needs_recovery;
};

struct fh_bgm_adapter {
    pthread_mutex_t mu;
    pthread_cond_t cv;
    struct fh_bgm_ops ops;
    struct fh_bgm_block memory;
    enum fh_bgm_state state;
    enum fh_bgm_mode mode;
    uint32_t width, height;
    uint32_t in_flight;
    uint32_t last_driver_status;
    uint64_t generation;
    uint64_t submitted, completed, rejected, stale, failures, recoveries;
    int memory_allocated;
    int mem_initialized;
    int enabled;
    int initialized;
};

int fh_bgm_adapter_init(struct fh_bgm_adapter *a, const struct fh_bgm_ops *ops);
int fh_bgm_adapter_start(struct fh_bgm_adapter *a, uint32_t width, uint32_t height,
                         enum fh_bgm_mode mode);
int fh_bgm_adapter_submit_sync(struct fh_bgm_adapter *a,
                               const struct fh_bgm_frame *frame,
                               struct fh_bgm_result *result);
/* Hot stop: native mode intentionally transitions to HELD after confirmed
 * DISABLE because no synchronous hardware drain is proven. */
int fh_bgm_adapter_stop(struct fh_bgm_adapter *a);
/* Explicit controlled-restart boundary. Caller must have stopped/unbound all
 * external producers/routes before invoking it. */
int fh_bgm_adapter_recover(struct fh_bgm_adapter *a);
/* Final process-owner release. Unlike hot stop, destroy may use driver release
 * to retire HELD/POISONED memory before destroying synchronization objects. */
int fh_bgm_adapter_destroy(struct fh_bgm_adapter *a);
int fh_bgm_adapter_needs_recovery(struct fh_bgm_adapter *a);
/* Thread-safe public read surface; callers should not inspect mutable adapter
 * fields directly while lifecycle operations may run concurrently. */
int fh_bgm_adapter_get_status(struct fh_bgm_adapter *a, struct fh_bgm_adapter_status *out);

#endif
