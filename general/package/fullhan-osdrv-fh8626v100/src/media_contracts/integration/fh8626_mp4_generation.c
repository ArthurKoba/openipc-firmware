#include "fh8626_mp4_generation.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

int fh_mp4_codec_update(struct fh_mp4_codec_generation *g,
                        const uint8_t *sps,size_t sps_len,
                        const uint8_t *pps,size_t pps_len)
{
    int changed;
    if(!g||!sps||!pps||sps_len<4||pps_len<2||sps_len>FH_MP4_PARAM_MAX||pps_len>FH_MP4_PARAM_MAX) return -EINVAL;
    changed=g->sps_len!=sps_len||g->pps_len!=pps_len||memcmp(g->sps,sps,sps_len)||memcmp(g->pps,pps,pps_len);
    if(!changed) return 0;
    if(g->generation==UINT64_MAX) return -EOVERFLOW;
    memcpy(g->sps,sps,sps_len);memcpy(g->pps,pps,pps_len);g->sps_len=sps_len;g->pps_len=pps_len;g->generation++;
    g->avcc_profile=sps[1];g->avcc_compat=sps[2];g->avcc_level=sps[3];g->header_len=0;g->header_generation=0;return 1;
}
int fh_mp4_codec_set_header(struct fh_mp4_codec_generation *g,uint64_t generation,size_t header_len)
{if(!g||generation!=g->generation||header_len==0)return -EINVAL;g->header_generation=generation;g->header_len=header_len;return 0;}
void fh_mp4_sink_init(struct fh_mp4_sink_generation *s,uint64_t session_id){if(!s)return;memset(s,0,sizeof(*s));s->session_id=session_id;s->waiting_idr=1;}
void fh_mp4_sink_sync_generation(struct fh_mp4_sink_generation *s,const struct fh_mp4_codec_generation *g){if(!s||!g)return;if(s->codec_generation!=g->generation){s->codec_generation=g->generation;s->header_sent=0;s->waiting_idr=1;}}
int fh_mp4_sink_accept_header(struct fh_mp4_sink_generation *s,const struct fh_mp4_codec_generation *g,uint64_t generation,size_t header_len){if(!s||!g||generation!=g->generation||g->header_generation!=generation||header_len==0||header_len!=g->header_len)return -EINVAL;fh_mp4_sink_sync_generation(s,g);s->header_sent=1;return 0;}
int fh_mp4_sink_begin_segment(struct fh_mp4_sink_generation *s,const struct fh_mp4_codec_generation *g){if(!s||!g)return -EINVAL;fh_mp4_sink_sync_generation(s,g);if(s->segment_seq==UINT64_MAX)return -EOVERFLOW;s->segment_seq++;s->header_sent=0;s->waiting_idr=1;return 0;}
int fh_mp4_sink_accept_sample(struct fh_mp4_sink_generation *s,const struct fh_mp4_codec_generation *g,const struct fh_mp4_sample_snapshot *sample){if(!s||!g||!sample||!sample->valid)return -EINVAL;fh_mp4_sink_sync_generation(s,g);if(!s->header_sent)return -EAGAIN;if(s->waiting_idr&&!sample->key)return -EAGAIN;if(sample->key)s->waiting_idr=0;return 0;}
int fh_mp4_segment_name(char *dst,size_t cap,const char *base,uint64_t session_id,uint64_t seq){int n;if(!dst||!cap||!base||!*base)return -EINVAL;n=snprintf(dst,cap,"%s.%llu.%llu.mp4",base,(unsigned long long)session_id,(unsigned long long)seq);return (n<0||(size_t)n>=cap)?-ENOSPC:0;}
