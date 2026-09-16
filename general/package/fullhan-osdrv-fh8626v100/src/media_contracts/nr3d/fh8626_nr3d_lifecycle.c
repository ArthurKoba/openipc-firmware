#include "fh8626_nr3d_lifecycle.h"

#include <errno.h>
#include <string.h>

static int lock_lifecycle(struct fh_nr3d_lifecycle *l)
{
    if (!l || !l->initialized)
        return -EINVAL;
    if (pthread_mutex_lock(&l->mutex) != 0)
        return -EDEADLK;
    return 0;
}

int fh_nr3d_lifecycle_init(struct fh_nr3d_lifecycle *l, struct fh_nr3d_kernel *kernel)
{
    if (!l || !kernel || !kernel->ioctl)
        return -EINVAL;
    memset(l, 0, sizeof(*l));
    if (pthread_mutex_init(&l->mutex, NULL) != 0)
        return -EAGAIN;
    l->kernel = kernel;
    l->state = FH_NR3D_FRESH;
    l->initialized = 1;
    return 0;
}

void fh_nr3d_lifecycle_destroy(struct fh_nr3d_lifecycle *l)
{
    if (!l || !l->initialized)
        return;
    pthread_mutex_destroy(&l->mutex);
    memset(l, 0, sizeof(*l));
}

int fh_nr3d_lifecycle_cold_start(struct fh_nr3d_lifecycle *l, int enabled)
{
    int rc = lock_lifecycle(l);
    if (rc)
        return rc;
    if (l->state != FH_NR3D_FRESH) {
        pthread_mutex_unlock(&l->mutex);
        return -EBUSY;
    }

    rc = fh_nr3d_kernel_set_verified(l->kernel, enabled != 0, NULL);
    l->last_driver_status = rc;
    if (rc) {
        /* A proc write followed by failed/mismatched readback has unknown
         * effective completion.  Do not infer a safe selector/history state. */
        l->state = FH_NR3D_POISONED;
        l->failures++;
        pthread_mutex_unlock(&l->mutex);
        return rc;
    }

    l->state = enabled ? FH_NR3D_COLD_ON : FH_NR3D_COLD_OFF;
    l->generation++;
    l->cold_starts++;
    pthread_mutex_unlock(&l->mutex);
    return 0;
}

int fh_nr3d_lifecycle_hot_disable(struct fh_nr3d_lifecycle *l)
{
    int rc = lock_lifecycle(l);
    if (rc)
        return rc;

    if (l->state == FH_NR3D_COLD_OFF || l->state == FH_NR3D_HOT_DISABLED) {
        pthread_mutex_unlock(&l->mutex);
        return 0;
    }
    if (l->state == FH_NR3D_POISONED || l->state == FH_NR3D_RESTART_REQUIRED) {
        pthread_mutex_unlock(&l->mutex);
        return -EUCLEAN;
    }
    if (l->state != FH_NR3D_COLD_ON) {
        pthread_mutex_unlock(&l->mutex);
        return -EINVAL;
    }

    rc = fh_nr3d_kernel_set_verified(l->kernel, 0, NULL);
    l->last_driver_status = rc;
    if (rc) {
        l->state = FH_NR3D_POISONED;
        l->failures++;
        pthread_mutex_unlock(&l->mutex);
        return rc;
    }

    l->state = FH_NR3D_HOT_DISABLED;
    l->hot_disables++;
    pthread_mutex_unlock(&l->mutex);
    return 0;
}

enum fh_nr3d_action_result
fh_nr3d_lifecycle_request_hot_enable(struct fh_nr3d_lifecycle *l)
{
    enum fh_nr3d_action_result result;

    if (lock_lifecycle(l))
        return FH_NR3D_ACTION_RECOVERY_REQUIRED;

    if (l->state == FH_NR3D_COLD_ON) {
        result = FH_NR3D_ACTION_OK;
    } else if (l->state == FH_NR3D_POISONED) {
        result = FH_NR3D_ACTION_RECOVERY_REQUIRED;
    } else if (l->state == FH_NR3D_COLD_OFF || l->state == FH_NR3D_HOT_DISABLED ||
               l->state == FH_NR3D_RESTART_REQUIRED) {
        l->state = FH_NR3D_RESTART_REQUIRED;
        l->restart_requests++;
        result = FH_NR3D_ACTION_RESTART_REQUIRED;
    } else {
        result = FH_NR3D_ACTION_RECOVERY_REQUIRED;
    }

    pthread_mutex_unlock(&l->mutex);
    return result;
}

enum fh_nr3d_action_result
fh_nr3d_lifecycle_request_history_boundary(struct fh_nr3d_lifecycle *l)
{
    enum fh_nr3d_action_result result;

    if (lock_lifecycle(l))
        return FH_NR3D_ACTION_RECOVERY_REQUIRED;

    if (l->state == FH_NR3D_POISONED) {
        result = FH_NR3D_ACTION_RECOVERY_REQUIRED;
    } else if (l->state == FH_NR3D_COLD_ON || l->state == FH_NR3D_HOT_DISABLED ||
               l->state == FH_NR3D_RESTART_REQUIRED) {
        l->state = FH_NR3D_RESTART_REQUIRED;
        l->restart_requests++;
        result = FH_NR3D_ACTION_RESTART_REQUIRED;
    } else if (l->state == FH_NR3D_COLD_OFF || l->state == FH_NR3D_FRESH) {
        result = FH_NR3D_ACTION_OK;
    } else {
        result = FH_NR3D_ACTION_RECOVERY_REQUIRED;
    }

    pthread_mutex_unlock(&l->mutex);
    return result;
}

int fh_nr3d_lifecycle_begin_after_external_restart(struct fh_nr3d_lifecycle *l)
{
    int rc = lock_lifecycle(l);
    if (rc)
        return rc;

    if (l->state != FH_NR3D_RESTART_REQUIRED && l->state != FH_NR3D_HOT_DISABLED &&
        l->state != FH_NR3D_POISONED && l->state != FH_NR3D_COLD_OFF &&
        l->state != FH_NR3D_COLD_ON) {
        pthread_mutex_unlock(&l->mutex);
        return -EINVAL;
    }

    /* No driver operation here.  This only records that the owner/integrator
     * has completed the restart boundary outside this standalone component. */
    l->state = FH_NR3D_FRESH;
    l->last_driver_status = 0;
    l->generation++;
    pthread_mutex_unlock(&l->mutex);
    return 0;
}

int fh_nr3d_lifecycle_needs_restart(struct fh_nr3d_lifecycle *l)
{
    int result = 0;
    if (lock_lifecycle(l))
        return 0;
    result = l->state == FH_NR3D_RESTART_REQUIRED || l->state == FH_NR3D_HOT_DISABLED;
    pthread_mutex_unlock(&l->mutex);
    return result;
}

int fh_nr3d_lifecycle_needs_recovery(struct fh_nr3d_lifecycle *l)
{
    int result = 1;
    if (lock_lifecycle(l))
        return 1;
    result = l->state == FH_NR3D_POISONED;
    pthread_mutex_unlock(&l->mutex);
    return result;
}

int fh_nr3d_lifecycle_get_state(struct fh_nr3d_lifecycle *l,
                                enum fh_nr3d_lifecycle_state *state,
                                uint64_t *generation)
{
    int rc;
    if (!state) return -EINVAL;
    rc = lock_lifecycle(l);
    if (rc) return rc;
    *state = l->state;
    if (generation) *generation = l->generation;
    pthread_mutex_unlock(&l->mutex);
    return 0;
}
