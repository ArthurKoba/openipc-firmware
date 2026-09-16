#define _GNU_SOURCE
#include "fh8626_bgm_adapter.h"
#include "fh8626_ghosting_policy.h"
#include "fh8626_h264_control.h"
#include "fh8626_nr3d_lifecycle.h"
#include "fh8626_h264_txn.h"
#include "fh8626_audio_fanout.h"
#include "fh8626_mp4_generation.h"
#include "fh8626_flv_generation.h"
#include "fh8626_rtmp_session.h"
#include "fh8626_timestamp.h"
#include "fh8626_bitrate_observer.h"
#include "fh8626_http_client.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Independent pass B: observe-mode generation fence + NR3D readback failure.
 * No route fake or pass-A state is shared. */
struct f {
    uint64_t result_pts;
    int recover_flag;
};
static int al(void *o, uint32_t n, uint32_t a, struct fh_bgm_block *b)
{
    (void)o; assert(a == 0x400u); b->phys=0x400000u; b->user=0x500000u; b->bytes=n; return 0;
}
static int fr(void *o, struct fh_bgm_block *b)
{
    (void)o; memset(b,0,sizeof(*b)); return 0;
}
static int io(void *o, unsigned long req, void *arg, uint32_t *raw)
{
    struct f *f=o; if(raw)*raw=0;
    if(req==FH_BGM_MEM_QUERY)((struct fh_bgm_mem_query*)arg)->bytes=0x4101u;
    if(req==FH_BGM_GET_SW_RESULT){struct fh_bgm_sw_result*r=arg;r->words[10]=(uint32_t)f->result_pts;r->words[11]=(uint32_t)(f->result_pts>>32);}
    return 0;
}
static int rel(void *o, struct fh_bgm_block *b){((struct f*)o)->recover_flag=0;memset(b,0,sizeof(*b));return 0;}
static int reopen(void *o){(void)o;return 0;}
static int needs(void *o){return ((struct f*)o)->recover_flag;}

struct nr { uint32_t mode; };
static int nio(void *o,unsigned long req,void *arg){struct nr*n=o;assert(req==FH_ISP_NR3D_QUERY);((struct fh_nr3d_driver_config*)arg)->mode=n->mode;return 0;}

struct hi { unsigned calls; };
static int hio(void *o,unsigned long req,void *arg){struct hi*h=o;(void)req;(void)arg;h->calls++;return 0;}

int main(void)
{
    char path[]="/tmp/fh_lifecycle_b_nr3d_XXXXXX";
    int fd=mkstemp(path);
    struct f f={0};
    struct fh_bgm_ops ops={al,fr,io,rel,reopen,needs,&f};
    struct fh_bgm_adapter a;
    struct fh_bgm_frame x={1280,720,0x600000u,0x1000u};
    struct fh_bgm_frame y=x;
    struct fh_bgm_result r;
    struct fh_ghosting_policy p;
    struct nr nf={0};
    struct fh_nr3d_kernel nk={nio,&nf,path};
    struct fh_nr3d_lifecycle nl;
    struct hi hf={0};
    struct fh_h264_control hc={hio,&hf,0};
    struct fh_pae_rc_realtime_wire bad={0,1u,(100u<<16)|500u,25u,52u,28u,50u};

    assert(fd>=0);close(fd);

    /* init */
    assert(!fh_bgm_adapter_init(&a,&ops));
    assert(!fh_nr3d_lifecycle_init(&nl,&nk));
    fh_ghosting_policy_defaults(&p);
    p.bgm_mode=FH_BGM_MODE_OBSERVE;

    /* run */
    assert(!fh_bgm_adapter_start(&a,1280,720,FH_BGM_MODE_OBSERVE));
    f.result_pts=x.pts;
    assert(!fh_bgm_adapter_submit_sync(&a,&x,&r));

    /* clean stop + restart establishes a new generation. */
    assert(!fh_bgm_adapter_stop(&a));
    assert(!fh_bgm_adapter_stop(&a));
    assert(!fh_bgm_adapter_start(&a,1280,720,FH_BGM_MODE_OBSERVE));

    /* error: delayed result from previous run must not be accepted. */
    y.pts=x.pts+1;
    f.result_pts=x.pts;
    assert(fh_bgm_adapter_submit_sync(&a,&y,&r)==-ESTALE);
    assert(a.state==FH_BGM_POISONED && a.stale==1);
    assert(fh_bgm_adapter_stop(&a)==-EUCLEAN);
    assert(fh_bgm_adapter_start(&a,1280,720,FH_BGM_MODE_OBSERVE)==-EUCLEAN);

    /* mode/sensor changes in this error state demand recovery, not a hot path. */
    assert(fh_ghosting_policy_decide_change(&p,FH_GHOSTING_CHANGE_MODE,a.state,
            FH_BGM_ROUTE_FRESH,FH_NR3D_COLD_OFF)==FH_GHOSTING_RECOVERY_REQUIRED);
    assert(fh_ghosting_policy_decide_change(&p,FH_GHOSTING_CHANGE_SENSOR,a.state,
            FH_BGM_ROUTE_FRESH,FH_NR3D_COLD_OFF)==FH_GHOSTING_RECOVERY_REQUIRED);

    /* explicit recovery + run */
    assert(!fh_bgm_adapter_recover(&a));
    assert(!fh_bgm_adapter_start(&a,1280,720,FH_BGM_MODE_OBSERVE));
    f.result_pts=y.pts;
    assert(!fh_bgm_adapter_submit_sync(&a,&y,&r));

    /* Independent NR3D negative: requested ON but readback says OFF -> poison. */
    nf.mode=0;
    assert(fh_nr3d_lifecycle_cold_start(&nl,1)==-EIO);
    assert(nl.state==FH_NR3D_POISONED);
    assert(fh_ghosting_policy_decide_change(&p,FH_GHOSTING_CHANGE_SENSOR,a.state,
            FH_BGM_ROUTE_FRESH,nl.state)==FH_GHOSTING_RECOVERY_REQUIRED);
    assert(!fh_nr3d_lifecycle_begin_after_external_restart(&nl));
    nf.mode=0;
    assert(!fh_nr3d_lifecycle_cold_start(&nl,0));
    assert(fh_nr3d_lifecycle_request_hot_enable(&nl)==FH_NR3D_ACTION_RESTART_REQUIRED);

    /* Invalid realtime RC is rejected before ioctl; mode cannot be supplied. */
    assert(fh_h264_change_rc_realtime(&hc,&bad)==-EINVAL);
    assert(hf.calls==0);

    /* Independent late-media contracts: ACK is staged, fan-out cursors are independent,
     * generation changes reset sink readiness, RTMP send is explicit, and timestamp
     * arithmetic stays unsigned at 0x80000000. */
    {
        struct fh_h264_txn tx; uint64_t tid;
        fh_h264_txn_init(&tx);
        assert(!fh_h264_txn_begin(&tx, 2, 10, &tid));
        assert(!fh_h264_txn_accept(&tx, tid));
        assert(!fh_h264_txn_applied(&tx, tid));
        assert(fh_h264_txn_output(&tx, tid, 10) == -EAGAIN);
        assert(!fh_h264_txn_output(&tx, tid, 11));
    }
    {
        struct fh_audio_fanout af; struct fh_audio_cursor c1,c2; struct fh_audio_frame a1,a2;
        char z='z'; fh_audio_fanout_init(&af, 1); fh_audio_cursor_subscribe(&af,&c1); fh_audio_cursor_subscribe(&af,&c2);
        assert(!fh_audio_fanout_publish(&af, 1, 80, 1, &z, 1, NULL));
        assert(fh_audio_cursor_read(&af,&c1,&a1)==1); assert(fh_audio_cursor_read(&af,&c2,&a2)==1);
        assert(a1.capture_seq==a2.capture_seq);
    }
    {
        struct fh_flv_codec_generation fg={0}; struct fh_flv_sink_generation fs;
        uint8_t sps[]={0x67,77,0,31}, pps[]={0x68,1};
        assert(fh_flv_codec_update(&fg,sps,sizeof(sps),pps,sizeof(pps),FH_FLV_RATE_UNKNOWN,0)==1);
        fh_flv_sink_init(&fs); assert(!fh_flv_sink_accept_metadata(&fs,&fg,fg.generation,1));
        assert(!fh_flv_sink_accept_header(&fs,&fg,fg.generation,1));
        sps[3]=32; assert(fh_flv_codec_update(&fg,sps,sizeof(sps),pps,sizeof(pps),FH_FLV_RATE_UNKNOWN,0)==1);
        fh_flv_sink_sync(&fs,&fg); assert(!fs.metadata_sent && !fs.header_sent && fs.waiting_idr);
    }
    {
        struct fh_rtmp_session rs; const struct fh_rtmp_packet *rp; char k='k';
        fh_rtmp_session_init(&rs, 16); assert(!fh_rtmp_session_start(&rs)); assert(!fh_rtmp_session_ready(&rs));
        assert(!fh_rtmp_session_enqueue(&rs,FH_RTMP_VIDEO,1,1,&k,1,NULL));
        assert(fh_rtmp_session_take(&rs,&rp)==1); assert(!fh_rtmp_session_send_result(&rs,0)); assert(!rs.need_idr);
    }
    {
        struct fh_timestamp_origin to; struct fh_rtmp_timestamp rt;
        assert(!fh_timestamp_origin_set(&to,1,0,(struct fh_timebase){1,1000}));
        assert(!fh_timestamp_rtmp(&to,1,0x80000000ULL,&rt)); assert(rt.ms==0x80000000U && rt.extended);
    }
    {
        struct fh_bitrate_window bw; struct fh_bitrate_report br;
        fh_bitrate_window_init(&bw,1,0);
        assert(!fh_bitrate_set_applied_target(&bw,1,0,2,1000000));
        assert(!fh_bitrate_record_producer(&bw,1,500,1000));
        assert(!fh_bitrate_record_network_sent(&bw,1,750,800));
        assert(!fh_bitrate_report(&bw,1,1000,&br));
        assert(br.applied_target_bps==1000000 && br.producer_bps==8000 && br.network_sent_bps==6400);
    }
    {
        struct fh_http_client hc1,hc2; const struct fh_http_packet *hp; char key='k';
        fh_http_client_init(&hc1,1,1,1,10); fh_http_client_init(&hc2,2,1,8,10);
        assert(!fh_http_client_set_generation(&hc1,1)); assert(!fh_http_client_set_generation(&hc2,1));
        assert(!fh_http_client_accept_header(&hc1,1,1)); assert(!fh_http_client_accept_header(&hc2,1,1));
        assert(!fh_http_client_enqueue(&hc1,FH_HTTP_VIDEO,1,1,0,&key,1,NULL));
        assert(!fh_http_client_enqueue(&hc2,FH_HTTP_VIDEO,1,1,0,&key,1,NULL));
        assert(fh_http_client_take(&hc1,10,&hp)==-ETIMEDOUT && hc1.state==FH_HTTP_FAILED);
        assert(fh_http_client_take(&hc2,1,&hp)==1 && hc2.state==FH_HTTP_READY);
        assert(!fh_http_client_send_result(&hc2,0));
    }

    assert(!fh_bgm_adapter_stop(&a));
    assert(!fh_bgm_adapter_destroy(&a));
    fh_nr3d_lifecycle_destroy(&nl);
    unlink(path);
    puts("Lifecycle pass B init/run/error/stop/restart/mode-change/sensor-change: PASS");
    return 0;
}
