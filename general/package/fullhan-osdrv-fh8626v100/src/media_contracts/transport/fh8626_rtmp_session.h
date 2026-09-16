#ifndef FH8626_RTMP_SESSION_H
#define FH8626_RTMP_SESSION_H

#include <stddef.h>
#include <stdint.h>

#define FH_RTMP_QUEUE_SLOTS 8U
#define FH_RTMP_PACKET_MAX 4096U

enum fh_rtmp_state { FH_RTMP_DISCONNECTED=0,FH_RTMP_STARTING,FH_RTMP_READY,FH_RTMP_STOPPING,FH_RTMP_FAILED };
enum fh_rtmp_packet_type { FH_RTMP_METADATA=1,FH_RTMP_AVC_HEADER,FH_RTMP_VIDEO,FH_RTMP_AUDIO };
struct fh_rtmp_packet {
    uint64_t seq;
    uint64_t epoch;
    uint64_t codec_generation;
    enum fh_rtmp_packet_type type;
    int key;
    size_t size;
    uint8_t bytes[FH_RTMP_PACKET_MAX];
};
struct fh_rtmp_session {
    enum fh_rtmp_state state;
    uint64_t epoch;
    uint64_t next_seq;
    uint64_t recovery_boundary_seq;
    size_t byte_limit;
    size_t queued_bytes;
    struct fh_rtmp_packet q[FH_RTMP_QUEUE_SLOTS];
    size_t head,count;
    int in_flight;
    int need_idr;
};

void fh_rtmp_session_init(struct fh_rtmp_session *s,size_t byte_limit);
int fh_rtmp_session_start(struct fh_rtmp_session *s);
int fh_rtmp_session_ready(struct fh_rtmp_session *s);
int fh_rtmp_session_enqueue(struct fh_rtmp_session *s,enum fh_rtmp_packet_type type,
                            uint64_t codec_generation,int key,const void *bytes,size_t size,uint64_t *seq);
int fh_rtmp_session_take(struct fh_rtmp_session *s,const struct fh_rtmp_packet **packet);
int fh_rtmp_session_send_result(struct fh_rtmp_session *s,int send_rc);
int fh_rtmp_session_remote_eof(struct fh_rtmp_session *s);
int fh_rtmp_session_begin_stop(struct fh_rtmp_session *s);
int fh_rtmp_session_reconnect(struct fh_rtmp_session *s);

#endif
