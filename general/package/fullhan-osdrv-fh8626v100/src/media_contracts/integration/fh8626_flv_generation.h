#ifndef FH8626_FLV_GENERATION_H
#define FH8626_FLV_GENERATION_H

#include <stddef.h>
#include <stdint.h>

enum fh_flv_bitrate_provenance {
    FH_FLV_RATE_UNKNOWN = 0,
    FH_FLV_RATE_APPLIED_TARGET = 1
};
struct fh_flv_codec_generation {
    uint64_t generation;
    uint8_t sps[128]; size_t sps_len;
    uint8_t pps[128]; size_t pps_len;
    uint8_t avcc_profile,avcc_compat,avcc_level;
    enum fh_flv_bitrate_provenance rate_provenance;
    uint32_t bitrate_kbps;
};
struct fh_flv_sink_generation {
    uint64_t codec_generation;
    int metadata_sent;
    int header_sent;
    int waiting_idr;
};
int fh_flv_codec_update(struct fh_flv_codec_generation *g,const uint8_t*sps,size_t sps_len,
                        const uint8_t*pps,size_t pps_len,enum fh_flv_bitrate_provenance prov,uint32_t bitrate_kbps);
void fh_flv_sink_init(struct fh_flv_sink_generation *s);
void fh_flv_sink_sync(struct fh_flv_sink_generation *s,const struct fh_flv_codec_generation *g);
int fh_flv_sink_accept_metadata(struct fh_flv_sink_generation *s,const struct fh_flv_codec_generation *g,uint64_t generation,size_t len);
int fh_flv_sink_accept_header(struct fh_flv_sink_generation *s,const struct fh_flv_codec_generation *g,uint64_t generation,size_t len);
int fh_flv_sink_accept_video(struct fh_flv_sink_generation *s,const struct fh_flv_codec_generation *g,int key);
#endif
