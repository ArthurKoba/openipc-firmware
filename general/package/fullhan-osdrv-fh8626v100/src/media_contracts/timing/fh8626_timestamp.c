#include "fh8626_timestamp.h"
#include <errno.h>
#include <limits.h>
static int tbok(struct fh_timebase tb){return tb.num!=0U&&tb.den!=0U;}
int fh_timestamp_origin_set(struct fh_timestamp_origin*o,uint64_t epoch,uint64_t origin_pts,struct fh_timebase tb){if(!o||!epoch||!tbok(tb))return -EINVAL;o->epoch=epoch;o->origin_pts=origin_pts;o->source_tb=tb;return 0;}
int fh_timestamp_rescale(uint64_t pts,struct fh_timebase src,uint32_t dst,uint64_t*out){unsigned __int128 n,q;if(!out||!dst||!tbok(src))return -EINVAL;n=(unsigned __int128)pts*src.num*dst;q=n/src.den;if(q>UINT64_MAX)return -EOVERFLOW;*out=(uint64_t)q;return 0;}
int fh_timestamp_rtmp(const struct fh_timestamp_origin*o,uint64_t epoch,uint64_t pts,struct fh_rtmp_timestamp*out){uint64_t ms;int rc;if(!o||!out||epoch!=o->epoch||pts<o->origin_pts)return -EINVAL;rc=fh_timestamp_rescale(pts-o->origin_pts,o->source_tb,1000,&ms);if(rc)return rc;if(ms>UINT32_MAX)return -ERANGE;out->ms=(uint32_t)ms;out->extended=out->ms>=0x00ffffffU;return 0;}
