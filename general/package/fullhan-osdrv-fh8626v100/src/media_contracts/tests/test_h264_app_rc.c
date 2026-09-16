#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "../h264/fh8626_h264_app_rc.h"

static struct fh_h264_app_rc base(uint32_t mode)
{
    struct fh_h264_app_rc a;
    memset(&a, 0, sizeof(a));
    a.mode = mode;
    a.init_qp = 26;
    a.bitrate = 8000;
    a.secondary_rate = 4000;
    a.frame_rate_packed = (25u << 16) | 1u;
    a.i_min_qp = 10; a.i_max_qp = 45;
    a.p_min_qp = 12; a.p_max_qp = 46;
    a.i_proportion = 1; a.p_proportion = 1;
    a.fluctuate_level = 2; a.ip_qp_delta = 0;
    a.max_rate_percent = 120;
    a.still_rate_percent = 50; a.max_still_qp = 45;
    a.fixed_i_qp = 28; a.fixed_p_qp = 30;
    return a;
}

int main(void)
{
    static const uint32_t modes[] = {3,4,5,6,11};
    static const uint32_t wire_modes[] = {0,1,2,4,5};
    size_t i;
    for (i = 0; i < sizeof(modes)/sizeof(modes[0]); ++i) {
        struct fh_h264_app_rc a = base(modes[i]), b;
        struct fh_pae_rc_wire w;
        assert(fh_h264_app_rc_to_wire(0, &a, &w) == 0);
        assert(w.rc_mode == wire_modes[i]);
        assert(fh_h264_wire_rc_to_app(&w, &b) == 0);
        assert(b.mode == a.mode);
    }
    {
        struct fh_pae_rc_wire w;
        struct fh_h264_app_rc a;
        memset(&w, 0, sizeof(w));
        w.rc_mode = FH_PAE_RC_MODE3_UNRESOLVED;
        assert(fh_h264_wire_rc_to_app(&w, &a) == -ENOTSUP);
    }
    {
        struct fh_h264_app_rc a = base(FH_APOLLO_RC_VBR);
        assert(fh_h264_app_rc_set_idr_qp(&a, 50) == 0);
        assert(a.init_qp == a.i_max_qp);
    }
    {
        uint32_t pub, app, wire;
        assert(fh_h264_public_rc_parse("qvbr", &pub) == 0 && pub == FH_PUBLIC_RC_QVBR);
        assert(fh_h264_apollo_rcmode_parse("qvbr", &app) < 0);
        assert(fh_h264_stock_resolve_app_mode(pub, FH_APOLLO_RC_CBR, &app) == 0 && app == FH_APOLLO_RC_AVBR);
        assert(fh_h264_app_mode_to_wire(app, &wire) == 0 && wire == FH_PAE_RC_AVBR);
        assert(fh_h264_stock_resolve_app_mode(FH_PUBLIC_RC_CBR, FH_APOLLO_RC_CVBR, &app) == 0 && app == FH_APOLLO_RC_CBR);
    }
    puts("test_h264_app_rc: PASS");
    return 0;
}
