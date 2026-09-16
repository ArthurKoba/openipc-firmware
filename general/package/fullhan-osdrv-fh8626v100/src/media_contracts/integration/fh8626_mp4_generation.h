#ifndef FH8626_MP4_GENERATION_H
#define FH8626_MP4_GENERATION_H

#include <stddef.h>
#include <stdint.h>
#include "../mux/fh8626_mp4_sample.h"

#define FH_MP4_PARAM_MAX 128U

struct fh_mp4_codec_generation {
    uint64_t generation;
    uint8_t sps[FH_MP4_PARAM_MAX]; size_t sps_len;
    uint8_t pps[FH_MP4_PARAM_MAX]; size_t pps_len;
    uint8_t avcc_profile, avcc_compat, avcc_level;
    size_t header_len;
    uint64_t header_generation;
};
struct fh_mp4_sink_generation {
    uint64_t codec_generation;
    uint64_t session_id;
    uint64_t segment_seq;
    int header_sent;
    int waiting_idr;
};

int fh_mp4_codec_update(struct fh_mp4_codec_generation *g,
                        const uint8_t *sps,size_t sps_len,
                        const uint8_t *pps,size_t pps_len);
int fh_mp4_codec_set_header(struct fh_mp4_codec_generation *g,uint64_t generation,size_t header_len);
void fh_mp4_sink_init(struct fh_mp4_sink_generation *s,uint64_t session_id);
void fh_mp4_sink_sync_generation(struct fh_mp4_sink_generation *s,const struct fh_mp4_codec_generation *g);
int fh_mp4_sink_accept_header(struct fh_mp4_sink_generation *s,const struct fh_mp4_codec_generation *g,
                              uint64_t generation,size_t header_len);
int fh_mp4_sink_begin_segment(struct fh_mp4_sink_generation *s,const struct fh_mp4_codec_generation *g);
int fh_mp4_sink_accept_sample(struct fh_mp4_sink_generation *s,const struct fh_mp4_codec_generation *g,
                              const struct fh_mp4_sample_snapshot *sample);
int fh_mp4_segment_name(char *dst,size_t cap,const char *base,uint64_t session_id,uint64_t seq);

#endif
