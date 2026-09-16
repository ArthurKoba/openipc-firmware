#ifndef FH8626_LENS_RECOVERY_H
#define FH8626_LENS_RECOVERY_H
#include <errno.h>
#include "fh8626_sensor_gc1054.h"
struct fh_lens_snapshot { uint32_t intt, gain; int valid; };
struct fh_lens_recovery_report {
    int select_error, intt_error, gain_error, readback_error;
};
typedef int (*fh_lens_select_fn)(void *, int);
typedef void (*fh_lens_settle_fn)(void *);
/* Serialized owner policy; not a claim of atomic stock lens switching.
 * A failed getter may still touch its output. Never publish a partial pair. */
static inline int fh_lens_snapshot_read(struct fh_sensor_gc1054 *sensor,
                                        struct fh_lens_snapshot *out)
{
    struct fh_lens_snapshot next = {0};
    int ri, rg;
    if (!out) return -EINVAL;
    out->valid = 0;
    if (!sensor) return -EINVAL;
    ri = fh_sensor_gc1054_get_intt(sensor, &next.intt);
    rg = fh_sensor_gc1054_get_gain(sensor, &next.gain);
    if (ri || rg) return ri ? ri : rg;
    next.valid = 1;
    *out = next;
    return 0;
}
/* Verify old mux before touching sensor controls. Try both independent
 * setters, then read back. Preserve every diagnostic and the first failure;
 * callback success alone is not proof that exposure was restored. */
static inline int fh_lens_restore(struct fh_sensor_gc1054 *sensor, int old_target,
                                  const struct fh_lens_snapshot *saved,
                                  fh_lens_select_fn select, fh_lens_settle_fn settle,
                                  void *opaque, struct fh_lens_recovery_report *r)
{
    struct fh_lens_snapshot actual = {0};
    if (!r) return -EINVAL;
    *r = (struct fh_lens_recovery_report){0};
    if (!sensor || !saved || !saved->valid || !select || !settle)
        return -EINVAL;
    r->select_error = select(opaque, old_target);
    if (r->select_error) return r->select_error;
    settle(opaque);
    r->intt_error = fh_sensor_gc1054_set_intt(sensor, saved->intt);
    r->gain_error = fh_sensor_gc1054_set_gain(sensor, saved->gain);
    r->readback_error = fh_lens_snapshot_read(sensor, &actual);
    if (!r->readback_error &&
        (actual.intt != saved->intt || actual.gain != saved->gain))
        r->readback_error = -EIO;
    return r->intt_error ? r->intt_error :
           r->gain_error ? r->gain_error : r->readback_error;
}
#endif
