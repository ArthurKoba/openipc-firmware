#ifndef FH8626_NR3D_LIFECYCLE_H
#define FH8626_NR3D_LIFECYCLE_H

#include <pthread.h>
#include <stdint.h>
#include "fh8626_nr3d_kernel.h"

/* NEW: software lifecycle intentionally distinguishes selector state from a
 * proven temporal-history reset.  HOT_DISABLED is not reusable as cold ON. */
enum fh_nr3d_lifecycle_state {
    FH_NR3D_FRESH = 0,
    FH_NR3D_COLD_OFF,
    FH_NR3D_COLD_ON,
    FH_NR3D_HOT_DISABLED,
    FH_NR3D_RESTART_REQUIRED,
    FH_NR3D_POISONED
};

enum fh_nr3d_action_result {
    FH_NR3D_ACTION_OK = 0,
    FH_NR3D_ACTION_RESTART_REQUIRED = 1,
    FH_NR3D_ACTION_RECOVERY_REQUIRED = 2
};

struct fh_nr3d_lifecycle {
    pthread_mutex_t mutex;
    struct fh_nr3d_kernel *kernel;
    enum fh_nr3d_lifecycle_state state;
    uint64_t generation;
    uint64_t cold_starts;
    uint64_t hot_disables;
    uint64_t restart_requests;
    uint64_t failures;
    int last_driver_status;
    int initialized;
};

int fh_nr3d_lifecycle_init(struct fh_nr3d_lifecycle *l, struct fh_nr3d_kernel *kernel);
void fh_nr3d_lifecycle_destroy(struct fh_nr3d_lifecycle *l);

/* Must be called only on a genuinely cold ISP epoch. */
int fh_nr3d_lifecycle_cold_start(struct fh_nr3d_lifecycle *l, int enabled);

/* RECONFIRMED safe policy direction: controlled ON -> OFF selector change.
 * Success does NOT mean temporal buffers were drained/reset. */
int fh_nr3d_lifecycle_hot_disable(struct fh_nr3d_lifecycle *l);

/* OFF -> ON is never performed here.  Returns RESTART_REQUIRED instead. */
enum fh_nr3d_action_result
fh_nr3d_lifecycle_request_hot_enable(struct fh_nr3d_lifecycle *l);

/* Profile/lens/sensor/geometry/mode changes call this before mutation. */
enum fh_nr3d_action_result
fh_nr3d_lifecycle_request_history_boundary(struct fh_nr3d_lifecycle *l);

/* Software epoch reset only.  The caller must invoke this AFTER an external
 * controlled ISP restart/recovery; it performs no hardware drain/reset. */
int fh_nr3d_lifecycle_begin_after_external_restart(struct fh_nr3d_lifecycle *l);

int fh_nr3d_lifecycle_needs_restart(struct fh_nr3d_lifecycle *l);
int fh_nr3d_lifecycle_needs_recovery(struct fh_nr3d_lifecycle *l);
int fh_nr3d_lifecycle_get_state(struct fh_nr3d_lifecycle *l,
                                enum fh_nr3d_lifecycle_state *state,
                                uint64_t *generation);

#endif
