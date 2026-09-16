#include "fh8626_ghosting_policy.h"

#include <errno.h>
#include <string.h>

void fh_ghosting_policy_defaults(struct fh_ghosting_policy *p)
{
    if (!p)
        return;
    p->nr3d_enabled = 0;
    p->bgm_mode = FH_BGM_MODE_OFF;
}

static int parse_bool(const char *s, int *out)
{
    if (!s || !*s)
        return 0;
    if (!strcmp(s, "1") || !strcmp(s, "on") || !strcmp(s, "yes")) {
        *out = 1;
        return 0;
    }
    if (!strcmp(s, "0") || !strcmp(s, "off") || !strcmp(s, "no")) {
        *out = 0;
        return 0;
    }
    return -EINVAL;
}

int fh_ghosting_policy_parse(struct fh_ghosting_policy *p,
                             const char *nr3d_value,
                             const char *bgm_value)
{
    int rc;
    if (!p)
        return -EINVAL;
    rc = parse_bool(nr3d_value, &p->nr3d_enabled);
    if (rc)
        return rc;
    if (!bgm_value || !*bgm_value)
        return 0;
    if (!strcmp(bgm_value, "off"))
        p->bgm_mode = FH_BGM_MODE_OFF;
    else if (!strcmp(bgm_value, "observe"))
        p->bgm_mode = FH_BGM_MODE_OBSERVE;
    else if (!strcmp(bgm_value, "encoder") || !strcmp(bgm_value, "native"))
        p->bgm_mode = FH_BGM_MODE_ENCODER_NATIVE;
    else
        return -EINVAL;
    return 0;
}

static int change_valid(enum fh_ghosting_change change)
{
    return change >= FH_GHOSTING_CHANGE_MODE && change <= FH_GHOSTING_CHANGE_GEOMETRY;
}

enum fh_ghosting_transition_decision
fh_ghosting_policy_decide_change(const struct fh_ghosting_policy *p,
                                 enum fh_ghosting_change change,
                                 enum fh_bgm_state bgm_state,
                                 enum fh_bgm_route_state route_state,
                                 enum fh_nr3d_lifecycle_state nr3d_state)
{
    if (!p || !change_valid(change))
        return FH_GHOSTING_RECOVERY_REQUIRED;

    /* Unknown-completion/held states require an explicit recovery boundary,
     * not merely a normal mode/sensor restart request. */
    if (route_state == FH_BGM_ROUTE_RECOVERY_REQUIRED ||
        bgm_state == FH_BGM_HELD || bgm_state == FH_BGM_POISONED ||
        bgm_state == FH_BGM_RECOVERY_REQUIRED || nr3d_state == FH_NR3D_POISONED)
        return FH_GHOSTING_RECOVERY_REQUIRED;

    if (nr3d_state == FH_NR3D_RESTART_REQUIRED || nr3d_state == FH_NR3D_HOT_DISABLED)
        return FH_GHOSTING_RESTART_REQUIRED;

    /* RECONFIRMED/NEW policy boundary: no proven universal drain/reset exists
     * for BGM/NR3D histories across these owner changes. */
    if (p->bgm_mode != FH_BGM_MODE_OFF || p->nr3d_enabled ||
        bgm_state == FH_BGM_RUNNING || nr3d_state == FH_NR3D_COLD_ON)
        return FH_GHOSTING_RESTART_REQUIRED;

    return FH_GHOSTING_HOT_OK;
}
