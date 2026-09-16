#include "fh8626_ghosting_policy.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>

static void assert_all_changes(const struct fh_ghosting_policy *p,
                               enum fh_bgm_state bgm,
                               enum fh_bgm_route_state route,
                               enum fh_nr3d_lifecycle_state nr3d,
                               enum fh_ghosting_transition_decision want)
{
    int change;
    for (change = FH_GHOSTING_CHANGE_MODE; change <= FH_GHOSTING_CHANGE_GEOMETRY; change++)
        assert(fh_ghosting_policy_decide_change(p, (enum fh_ghosting_change)change,
                                                bgm, route, nr3d) == want);
}

int main(void)
{
    struct fh_ghosting_policy p;

    fh_ghosting_policy_defaults(&p);
    assert(!p.nr3d_enabled && p.bgm_mode == FH_BGM_MODE_OFF);
    assert_all_changes(&p, FH_BGM_CLOSED, FH_BGM_ROUTE_FRESH, FH_NR3D_COLD_OFF,
                       FH_GHOSTING_HOT_OK);

    assert(!fh_ghosting_policy_parse(&p, "on", "encoder"));
    assert(p.nr3d_enabled && p.bgm_mode == FH_BGM_MODE_ENCODER_NATIVE);
    assert_all_changes(&p, FH_BGM_RUNNING, FH_BGM_ROUTE_BOUND, FH_NR3D_COLD_ON,
                       FH_GHOSTING_RESTART_REQUIRED);

    /* Sensor/mode/profile/geometry change with active BGM alone -> restart. */
    fh_ghosting_policy_defaults(&p);
    p.bgm_mode = FH_BGM_MODE_OBSERVE;
    assert_all_changes(&p, FH_BGM_RUNNING, FH_BGM_ROUTE_VERIFIED, FH_NR3D_COLD_OFF,
                       FH_GHOSTING_RESTART_REQUIRED);

    /* Active NR3D alone -> restart. */
    fh_ghosting_policy_defaults(&p);
    p.nr3d_enabled = 1;
    assert_all_changes(&p, FH_BGM_CLOSED, FH_BGM_ROUTE_FRESH, FH_NR3D_COLD_ON,
                       FH_GHOSTING_RESTART_REQUIRED);

    /* Unknown completion/held memory is stronger than a normal restart. */
    fh_ghosting_policy_defaults(&p);
    assert_all_changes(&p, FH_BGM_POISONED, FH_BGM_ROUTE_FRESH, FH_NR3D_COLD_OFF,
                       FH_GHOSTING_RECOVERY_REQUIRED);
    assert_all_changes(&p, FH_BGM_HELD, FH_BGM_ROUTE_FRESH, FH_NR3D_COLD_OFF,
                       FH_GHOSTING_RECOVERY_REQUIRED);
    assert_all_changes(&p, FH_BGM_CLOSED, FH_BGM_ROUTE_RECOVERY_REQUIRED,
                       FH_NR3D_COLD_OFF, FH_GHOSTING_RECOVERY_REQUIRED);
    assert_all_changes(&p, FH_BGM_CLOSED, FH_BGM_ROUTE_FRESH, FH_NR3D_POISONED,
                       FH_GHOSTING_RECOVERY_REQUIRED);

    assert(fh_ghosting_policy_parse(&p, "maybe", "off") == -EINVAL);
    assert(fh_ghosting_policy_parse(&p, "off", "bogus") == -EINVAL);
    assert(fh_ghosting_policy_decide_change(NULL, FH_GHOSTING_CHANGE_SENSOR,
                                             FH_BGM_CLOSED, FH_BGM_ROUTE_FRESH,
                                             FH_NR3D_COLD_OFF) == FH_GHOSTING_RECOVERY_REQUIRED);

    puts("Ghosting mode/profile/sensor/geometry restart/recovery policy: PASS");
    return 0;
}
