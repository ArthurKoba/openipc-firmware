#ifndef FH8626_GHOSTING_POLICY_H
#define FH8626_GHOSTING_POLICY_H

#include "fh8626_bgm_adapter.h"
#include "fh8626_bgm_stock_route.h"
#include "fh8626_nr3d_lifecycle.h"

struct fh_ghosting_policy {
    int nr3d_enabled;
    enum fh_bgm_mode bgm_mode;
};

enum fh_ghosting_change {
    FH_GHOSTING_CHANGE_MODE = 0,
    FH_GHOSTING_CHANGE_PROFILE,
    FH_GHOSTING_CHANGE_SENSOR,
    FH_GHOSTING_CHANGE_GEOMETRY
};

enum fh_ghosting_transition_decision {
    FH_GHOSTING_HOT_OK = 0,
    FH_GHOSTING_RESTART_REQUIRED = 1,
    FH_GHOSTING_RECOVERY_REQUIRED = 2
};

/* Defaults are conservative until target A/B: NR3D off, BGM off. */
void fh_ghosting_policy_defaults(struct fh_ghosting_policy *p);
int fh_ghosting_policy_parse(struct fh_ghosting_policy *p,
                             const char *nr3d_value,
                             const char *bgm_value);

/* NEW conservative mode/profile/sensor/geometry gate.  This function only
 * classifies the boundary; it never performs a hardware restart/reset. */
enum fh_ghosting_transition_decision
fh_ghosting_policy_decide_change(const struct fh_ghosting_policy *p,
                                 enum fh_ghosting_change change,
                                 enum fh_bgm_state bgm_state,
                                 enum fh_bgm_route_state route_state,
                                 enum fh_nr3d_lifecycle_state nr3d_state);

#endif
