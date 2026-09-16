#include "fh8626_bgm_adapter.h"
#include <errno.h>
#include <string.h>

static int callio_raw(struct fh_bgm_adapter *a, unsigned long req, void *arg,
                      uint32_t *raw_out)
{
    uint32_t raw = 0;
    int rc = a->ops.ioctl(a->ops.opaque, req, arg, &raw);
    if (raw_out) *raw_out = raw;
    return rc;
}

static int callio_locked(struct fh_bgm_adapter *a, unsigned long req, void *arg)
{
    uint32_t raw = 0;
    int rc = callio_raw(a, req, arg, &raw);
    a->last_driver_status = raw;
    return rc;
}

static void poison_locked(struct fh_bgm_adapter *a)
{
    a->state = FH_BGM_POISONED;
    a->failures++;
}

static void reset_closed_locked(struct fh_bgm_adapter *a)
{
    a->state = FH_BGM_CLOSED;
    a->mode = FH_BGM_MODE_OFF;
    a->width = 0;
    a->height = 0;
    a->enabled = 0;
    a->mem_initialized = 0;
}

static int free_memory_locked(struct fh_bgm_adapter *a)
{
    int rc;
    if (!a->memory_allocated) return 0;
    rc = a->ops.free(a->ops.opaque, &a->memory);
    if (rc) {
        a->state = FH_BGM_RECOVERY_REQUIRED;
        a->failures++;
        return rc;
    }
    memset(&a->memory, 0, sizeof(a->memory));
    a->memory_allocated = 0;
    return 0;
}

/* Roll back a start after MEM_INIT may have touched driver state. Memory is
 * released only when MEM_UNINIT is positively confirmed. */
static void rollback_start_locked(struct fh_bgm_adapter *a,
                                  int enable_may_have_happened,
                                  int mem_may_have_initialized)
{
    int rc;
    if (enable_may_have_happened) {
        rc = callio_locked(a, FH_BGM_DISABLE, NULL);
        if (rc) {
            poison_locked(a);
            return;
        }
        a->enabled = 0;
    }
    if (mem_may_have_initialized) {
        rc = callio_locked(a, FH_BGM_MEM_UNINIT, NULL);
        if (rc) {
            poison_locked(a);
            return;
        }
        a->mem_initialized = 0;
    }
    if (free_memory_locked(a)) return;
    reset_closed_locked(a);
}

int fh_bgm_adapter_init(struct fh_bgm_adapter *a, const struct fh_bgm_ops *ops)
{
    int rc;
    if (!a || !ops || !ops->alloc || !ops->free || !ops->ioctl ||
        !ops->final_release || !ops->reopen || !ops->backend_needs_recovery)
        return -EINVAL;
    memset(a, 0, sizeof(*a));
    a->ops = *ops;
    a->state = FH_BGM_CLOSED;
    rc = pthread_mutex_init(&a->mu, NULL);
    if (rc) return -rc;
    rc = pthread_cond_init(&a->cv, NULL);
    if (rc) {
        pthread_mutex_destroy(&a->mu);
        return -rc;
    }
    a->initialized = 1;
    return 0;
}

int fh_bgm_adapter_start(struct fh_bgm_adapter *a, uint32_t width, uint32_t height,
                         enum fh_bgm_mode mode)
{
    struct fh_bgm_mem_query q = { width, height, 0 };
    struct fh_bgm_mem_init m;
    struct fh_bgm_vi_attr vi = { width, height };
    int rc;

    if (!a || !a->initialized || !width || !height) return -EINVAL;
    if (mode != FH_BGM_MODE_OFF && mode != FH_BGM_MODE_OBSERVE &&
        mode != FH_BGM_MODE_ENCODER_NATIVE)
        return -EINVAL;

    pthread_mutex_lock(&a->mu);
    if (mode == FH_BGM_MODE_OFF) {
        rc = a->state == FH_BGM_CLOSED ? 0 : -EBUSY;
        pthread_mutex_unlock(&a->mu);
        return rc;
    }
    if (a->state == FH_BGM_HELD || a->state == FH_BGM_POISONED ||
        a->state == FH_BGM_RECOVERY_REQUIRED) {
        pthread_mutex_unlock(&a->mu);
        return -EUCLEAN;
    }
    if (a->state != FH_BGM_CLOSED) {
        pthread_mutex_unlock(&a->mu);
        return -EBUSY;
    }

    a->state = FH_BGM_STARTING;
    rc = callio_locked(a, FH_BGM_MEM_QUERY, &q);
    if (rc || !q.bytes) {
        reset_closed_locked(a);
        pthread_mutex_unlock(&a->mu);
        return rc ? rc : -EPROTO;
    }

    rc = a->ops.alloc(a->ops.opaque, q.bytes, 0x400u, &a->memory);
    if (rc) {
        if (a->ops.backend_needs_recovery(a->ops.opaque)) {
            a->state = FH_BGM_RECOVERY_REQUIRED;
            a->failures++;
        } else {
            reset_closed_locked(a);
        }
        pthread_mutex_unlock(&a->mu);
        return rc;
    }
    a->memory_allocated = 1;
    if (a->memory.bytes < q.bytes || !a->memory.phys || !a->memory.user) {
        rc = -ENOMEM;
        (void)free_memory_locked(a);
        if (a->state != FH_BGM_RECOVERY_REQUIRED) reset_closed_locked(a);
        pthread_mutex_unlock(&a->mu);
        return rc;
    }
    if (a->memory.user > UINT32_MAX) {
        rc = -EOVERFLOW;
        (void)free_memory_locked(a);
        if (a->state != FH_BGM_RECOVERY_REQUIRED) reset_closed_locked(a);
        pthread_mutex_unlock(&a->mu);
        return rc;
    }

    memset(&m, 0, sizeof(m));
    m.phys_base = a->memory.phys;
    m.user_base = (uint32_t)a->memory.user;
    /* RECONFIRMED: ABI size is exact MEM_QUERY bytes, never allocator rounding. */
    m.bytes = q.bytes;
    m.width = width;
    m.height = height;

    rc = callio_locked(a, FH_BGM_MEM_INIT, &m);
    if (rc) {
        /* NEW: MEM_INIT failure itself has unknown completion. */
        rollback_start_locked(a, 0, 1);
        pthread_mutex_unlock(&a->mu);
        return rc;
    }
    a->mem_initialized = 1;
    a->state = FH_BGM_READY;

    rc = callio_locked(a, FH_BGM_SET_VI_ATTR, &vi);
    if (rc) {
        rollback_start_locked(a, 0, 1);
        pthread_mutex_unlock(&a->mu);
        return rc;
    }

    rc = callio_locked(a, FH_BGM_ENABLE, NULL);
    if (rc) {
        /* ENABLE may have partially taken effect despite an error result. */
        rollback_start_locked(a, 1, 1);
        pthread_mutex_unlock(&a->mu);
        return rc;
    }
    a->enabled = 1;
    a->width = width;
    a->height = height;
    a->mode = mode;
    a->state = FH_BGM_RUNNING;
    a->generation++;
    if (!a->generation) a->generation++;
    pthread_mutex_unlock(&a->mu);
    return 0;
}

int fh_bgm_adapter_submit_sync(struct fh_bgm_adapter *a,
                               const struct fh_bgm_frame *frame,
                               struct fh_bgm_result *result)
{
    struct fh_bgm_submit s;
    struct fh_bgm_sw_result r;
    uint64_t got = 0, generation;
    uint32_t raw = 0;
    int rc;

    if (!a || !frame || !result || !frame->y_phys || !frame->pts) return -EINVAL;
    pthread_mutex_lock(&a->mu);
    if (a->state != FH_BGM_RUNNING) {
        a->rejected++;
        pthread_mutex_unlock(&a->mu);
        return -ESHUTDOWN;
    }
    if (a->mode == FH_BGM_MODE_ENCODER_NATIVE) {
        a->rejected++;
        pthread_mutex_unlock(&a->mu);
        return -EOPNOTSUPP;
    }
    if (a->in_flight) {
        a->rejected++;
        pthread_mutex_unlock(&a->mu);
        return -EAGAIN;
    }
    if (frame->width != a->width || frame->height != a->height) {
        a->rejected++;
        pthread_mutex_unlock(&a->mu);
        return -EINVAL;
    }
    a->in_flight = 1;
    a->submitted++;
    generation = a->generation;
    pthread_mutex_unlock(&a->mu);

    memset(&s, 0, sizeof(s));
    s.width = frame->width;
    s.height = frame->height;
    s.y_phys = frame->y_phys;
    s.pts_lo = (uint32_t)frame->pts;
    s.pts_hi = (uint32_t)(frame->pts >> 32);

    rc = callio_raw(a, FH_BGM_SUBMIT, &s, &raw);
    if (!rc) {
        memset(&r, 0, sizeof(r));
        rc = callio_raw(a, FH_BGM_GET_SW_RESULT, &r, &raw);
    }

    pthread_mutex_lock(&a->mu);
    a->last_driver_status = raw;
    if (rc) {
        /* SUBMIT/result failure leaves completion unknown. */
        poison_locked(a);
    } else {
        got = (uint64_t)r.words[10] | ((uint64_t)r.words[11] << 32);
        if (generation != a->generation || got != frame->pts) {
            a->stale++;
            poison_locked(a);
            rc = -ESTALE;
        } else {
            result->raw = r;
            result->pts = got;
            result->generation = generation;
            a->completed++;
        }
    }
    a->in_flight = 0;
    pthread_cond_broadcast(&a->cv);
    pthread_mutex_unlock(&a->mu);
    return rc;
}

int fh_bgm_adapter_stop(struct fh_bgm_adapter *a)
{
    int rc;
    if (!a || !a->initialized) return -EINVAL;
    pthread_mutex_lock(&a->mu);

    if (a->state == FH_BGM_CLOSED || a->state == FH_BGM_HELD) {
        pthread_mutex_unlock(&a->mu);
        return 0;
    }
    if (a->state == FH_BGM_RECOVERY_REQUIRED) {
        pthread_mutex_unlock(&a->mu);
        return -EUCLEAN;
    }
    if (a->state == FH_BGM_POISONED) {
        /* NEW: best-effort disable only; unknown completion forbids MEM_UNINIT/free. */
        if (a->memory_allocated || a->mem_initialized || a->enabled)
            (void)callio_locked(a, FH_BGM_DISABLE, NULL);
        pthread_mutex_unlock(&a->mu);
        return -EUCLEAN;
    }
    if (a->state != FH_BGM_RUNNING) {
        pthread_mutex_unlock(&a->mu);
        return -EBUSY;
    }

    a->state = FH_BGM_STOPPING;
    while (a->in_flight) pthread_cond_wait(&a->cv, &a->mu);
    if (a->state == FH_BGM_POISONED) {
        (void)callio_locked(a, FH_BGM_DISABLE, NULL);
        pthread_mutex_unlock(&a->mu);
        return -EUCLEAN;
    }

    rc = callio_locked(a, FH_BGM_DISABLE, NULL);
    if (rc) {
        poison_locked(a);
        pthread_mutex_unlock(&a->mu);
        return rc;
    }
    a->enabled = 0;

    if (a->mode == FH_BGM_MODE_ENCODER_NATIVE) {
        /* RECONFIRMED: bgm_disable() can complete physical disable later in IRQ;
         * without a proven synchronous drain, preserve backing memory. */
        a->state = FH_BGM_HELD;
        a->mode = FH_BGM_MODE_OFF;
        pthread_mutex_unlock(&a->mu);
        return 0;
    }

    rc = callio_locked(a, FH_BGM_MEM_UNINIT, NULL);
    if (rc) {
        poison_locked(a);
        pthread_mutex_unlock(&a->mu);
        return rc;
    }
    a->mem_initialized = 0;
    rc = free_memory_locked(a);
    if (rc) {
        pthread_mutex_unlock(&a->mu);
        return rc;
    }
    reset_closed_locked(a);
    pthread_mutex_unlock(&a->mu);
    return 0;
}

int fh_bgm_adapter_recover(struct fh_bgm_adapter *a)
{
    int rc;
    if (!a || !a->initialized) return -EINVAL;
    pthread_mutex_lock(&a->mu);
    if (a->state != FH_BGM_HELD && a->state != FH_BGM_POISONED &&
        a->state != FH_BGM_RECOVERY_REQUIRED) {
        pthread_mutex_unlock(&a->mu);
        return -EINVAL;
    }
    while (a->in_flight) pthread_cond_wait(&a->cv, &a->mu);

    rc = a->ops.final_release(a->ops.opaque, &a->memory);
    if (rc) {
        poison_locked(a);
        pthread_mutex_unlock(&a->mu);
        return rc;
    }
    memset(&a->memory, 0, sizeof(a->memory));
    a->memory_allocated = 0;
    a->mem_initialized = 0;
    a->enabled = 0;
    a->mode = FH_BGM_MODE_OFF;
    a->width = a->height = 0;

    rc = a->ops.reopen(a->ops.opaque);
    if (rc) {
        a->state = FH_BGM_RECOVERY_REQUIRED;
        a->failures++;
        pthread_mutex_unlock(&a->mu);
        return rc;
    }
    a->state = FH_BGM_CLOSED;
    a->generation++;
    if (!a->generation) a->generation++;
    a->recoveries++;
    pthread_mutex_unlock(&a->mu);
    return 0;
}

int fh_bgm_adapter_destroy(struct fh_bgm_adapter *a)
{
    int rc = 0;
    if (!a || !a->initialized) return -EINVAL;

    /* Stop performs all state inspection under the mutex.  It is safe to call
     * for CLOSED/HELD/POISONED states and avoids an unlocked state read here. */
    (void)fh_bgm_adapter_stop(a);

    pthread_mutex_lock(&a->mu);
    while (a->in_flight) pthread_cond_wait(&a->cv, &a->mu);
    /* Final process boundary is intentionally stronger than hot stop. The
     * backend must close /dev/bgm before releasing the dedicated VMM owner. */
    rc = a->ops.final_release(a->ops.opaque, &a->memory);
    if (rc) {
        poison_locked(a);
        pthread_mutex_unlock(&a->mu);
        return rc;
    }
    memset(&a->memory, 0, sizeof(a->memory));
    a->memory_allocated = 0;
    a->mem_initialized = 0;
    a->enabled = 0;
    reset_closed_locked(a);
    pthread_mutex_unlock(&a->mu);

    pthread_cond_destroy(&a->cv);
    pthread_mutex_destroy(&a->mu);
    a->initialized = 0;
    return 0;
}

int fh_bgm_adapter_needs_recovery(struct fh_bgm_adapter *a)
{
    int result;
    if (!a || !a->initialized) return 0;
    pthread_mutex_lock(&a->mu);
    result = a->state == FH_BGM_HELD || a->state == FH_BGM_POISONED ||
             a->state == FH_BGM_RECOVERY_REQUIRED ||
             a->ops.backend_needs_recovery(a->ops.opaque);
    pthread_mutex_unlock(&a->mu);
    return result;
}

int fh_bgm_adapter_get_status(struct fh_bgm_adapter *a, struct fh_bgm_adapter_status *out)
{
    if (!a || !out || !a->initialized) return -EINVAL;
    pthread_mutex_lock(&a->mu);
    memset(out, 0, sizeof(*out));
    out->state = a->state;
    out->mode = a->mode;
    out->last_driver_status = a->last_driver_status;
    out->in_flight = a->in_flight;
    out->generation = a->generation;
    out->submitted = a->submitted;
    out->completed = a->completed;
    out->rejected = a->rejected;
    out->stale = a->stale;
    out->failures = a->failures;
    out->recoveries = a->recoveries;
    out->memory_allocated = a->memory_allocated;
    out->mem_initialized = a->mem_initialized;
    out->enabled = a->enabled;
    out->needs_recovery = a->state == FH_BGM_HELD || a->state == FH_BGM_POISONED ||
                          a->state == FH_BGM_RECOVERY_REQUIRED ||
                          a->ops.backend_needs_recovery(a->ops.opaque);
    pthread_mutex_unlock(&a->mu);
    return 0;
}
