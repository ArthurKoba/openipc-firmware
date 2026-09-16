#ifndef FH8626_TIMESTAMP_H
#define FH8626_TIMESTAMP_H

#include <stdint.h>

struct fh_timebase { uint32_t num; uint32_t den; };
struct fh_timestamp_origin {
    uint64_t epoch;
    uint64_t origin_pts;
    struct fh_timebase source_tb;
};
struct fh_rtmp_timestamp {
    uint32_t ms;
    int extended;
};
int fh_timestamp_origin_set(struct fh_timestamp_origin *o,uint64_t epoch,uint64_t origin_pts,struct fh_timebase tb);
int fh_timestamp_rtmp(const struct fh_timestamp_origin *o,uint64_t epoch,uint64_t pts,struct fh_rtmp_timestamp *out);
int fh_timestamp_rescale(uint64_t pts,struct fh_timebase src,uint32_t dst_timescale,uint64_t *out);
#endif
