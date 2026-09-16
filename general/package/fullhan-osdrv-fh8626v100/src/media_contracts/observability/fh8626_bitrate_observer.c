#include "fh8626_bitrate_observer.h"

#include <errno.h>
#include <string.h>

void fh_bitrate_window_init(struct fh_bitrate_window *w,uint64_t epoch,uint64_t start_ms)
{
    if(!w) return;
    memset(w,0,sizeof(*w));
    w->epoch=epoch;
    w->start_ms=start_ms;
    w->last_ms=start_ms;
}

static int touch(struct fh_bitrate_window *w,uint64_t epoch,uint64_t now_ms)
{
    if(!w || !epoch || epoch!=w->epoch || now_ms<w->last_ms) return -EINVAL;
    w->last_ms=now_ms;
    return 0;
}
static int add(uint64_t *dst,uint64_t v)
{
    if(UINT64_MAX-*dst<v) return -EOVERFLOW;
    *dst+=v;
    return 0;
}
int fh_bitrate_set_applied_target(struct fh_bitrate_window *w,uint64_t epoch,uint64_t now_ms,uint64_t gen,uint64_t bps)
{int rc=touch(w,epoch,now_ms);if(rc)return rc;if(!gen)return -EINVAL;w->applied_config_generation=gen;w->applied_target_bps=bps;return 0;}
int fh_bitrate_set_output_generation(struct fh_bitrate_window *w,uint64_t epoch,uint64_t now_ms,uint64_t gen)
{int rc=touch(w,epoch,now_ms);if(rc)return rc;if(!gen)return -EINVAL;w->output_generation=gen;return 0;}
#define REC_FN(name,field) int name(struct fh_bitrate_window*w,uint64_t e,uint64_t n,uint64_t b){int rc=touch(w,e,n);if(rc)return rc;return add(&w->field,b);}
REC_FN(fh_bitrate_record_mux,mux_bytes)
REC_FN(fh_bitrate_record_file,file_bytes)
REC_FN(fh_bitrate_record_network_queued,network_queued_bytes)
REC_FN(fh_bitrate_record_network_sent,network_sent_bytes)
REC_FN(fh_bitrate_record_network_failed,network_failed_bytes)
#undef REC_FN
int fh_bitrate_record_producer(struct fh_bitrate_window*w,uint64_t e,uint64_t n,uint64_t b){int rc=touch(w,e,n);if(rc)return rc;if((rc=add(&w->producer_bytes,b)))return rc;return add(&w->producer_aus,1);}
int fh_bitrate_record_drop(struct fh_bitrate_window*w,uint64_t e,uint64_t n,uint64_t b){int rc=touch(w,e,n);if(rc)return rc;if((rc=add(&w->producer_dropped_bytes,b)))return rc;return add(&w->dropped_aus,1);}
static int rate(uint64_t bytes,uint64_t ms,uint64_t*out){unsigned __int128 n;if(!out||!ms)return -EINVAL;n=(unsigned __int128)bytes*8U*1000U;n/=ms;if(n>UINT64_MAX)return -EOVERFLOW;*out=(uint64_t)n;return 0;}
int fh_bitrate_report(const struct fh_bitrate_window*w,uint64_t e,uint64_t end,struct fh_bitrate_report*out){uint64_t d;int rc;if(!w||!out||e!=w->epoch||end<=w->start_ms||end<w->last_ms)return -EINVAL;d=end-w->start_ms;memset(out,0,sizeof(*out));out->epoch=e;out->duration_ms=d;out->applied_config_generation=w->applied_config_generation;out->output_generation=w->output_generation;out->applied_target_bps=w->applied_target_bps;out->producer_aus=w->producer_aus;out->dropped_aus=w->dropped_aus;
#define R(field,outfield) do{rc=rate(w->field,d,&out->outfield);if(rc)return rc;}while(0)
R(producer_bytes,producer_bps);R(producer_dropped_bytes,producer_drop_bps);R(mux_bytes,mux_bps);R(file_bytes,file_bps);R(network_queued_bytes,network_queued_bps);R(network_sent_bytes,network_sent_bps);R(network_failed_bytes,network_failed_bps);
#undef R
return 0;}
