#ifndef FH8626_MP4_SAMPLE_H
#define FH8626_MP4_SAMPLE_H

#include <stddef.h>
#include <stdint.h>
#include "../h264/fh8626_h264_stream.h"

struct fh_mp4_sample_snapshot {
    uint8_t *bytes;
    size_t capacity;
    size_t size;
    uint64_t pts;
    uint64_t source_generation;
    int valid;
    int key;
    size_t sps_offset, sps_len;
    size_t pps_offset, pps_len;
};

/* 1 = complete VCL sample published, 0 = valid AU but no VCL sample, <0 = error. */
int fh_mp4_sample_from_au(const struct fh_h264_au_snapshot *au,
                          struct fh_mp4_sample_snapshot *out);

#endif
