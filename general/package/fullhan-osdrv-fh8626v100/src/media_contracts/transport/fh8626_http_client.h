#ifndef FH8626_HTTP_CLIENT_H
#define FH8626_HTTP_CLIENT_H

#include <stddef.h>
#include <stdint.h>

#define FH_HTTP_QUEUE_SLOTS 8U
#define FH_HTTP_PACKET_MAX 4096U

enum fh_http_state {
    FH_HTTP_CLOSED = 0,
    FH_HTTP_READY,
    FH_HTTP_CLOSING,
    FH_HTTP_FAILED
};

enum fh_http_packet_type {
    FH_HTTP_HEADER = 1,
    FH_HTTP_VIDEO,
    FH_HTTP_AUDIO
};

struct fh_http_packet {
    uint64_t seq;
    uint64_t epoch;
    uint64_t codec_generation;
    uint64_t deadline_ms;
    enum fh_http_packet_type type;
    int key;
    size_t size;
    uint8_t bytes[FH_HTTP_PACKET_MAX];
};

struct fh_http_client {
    uint64_t client_id;
    uint64_t epoch;
    uint64_t codec_generation;
    uint64_t next_seq;
    uint64_t recovery_boundary_seq;
    uint64_t send_timeout_ms;
    size_t byte_limit;
    size_t queued_bytes;
    struct fh_http_packet q[FH_HTTP_QUEUE_SLOTS];
    size_t head;
    size_t count;
    int in_flight;
    int need_idr;
    int header_ready;
    enum fh_http_state state;
};

void fh_http_client_init(struct fh_http_client *c,uint64_t client_id,uint64_t epoch,
                         size_t byte_limit,uint64_t send_timeout_ms);
int fh_http_client_set_generation(struct fh_http_client *c,uint64_t generation);
int fh_http_client_accept_header(struct fh_http_client *c,uint64_t generation,size_t len);
int fh_http_client_enqueue(struct fh_http_client *c,enum fh_http_packet_type type,
                           uint64_t generation,int key,uint64_t now_ms,
                           const void *bytes,size_t size,uint64_t *seq);
/* 1 packet returned, 0 queue empty, <0 client failure/deadline. */
int fh_http_client_take(struct fh_http_client *c,uint64_t now_ms,const struct fh_http_packet **packet);
int fh_http_client_send_result(struct fh_http_client *c,int send_rc);
int fh_http_client_begin_close(struct fh_http_client *c);
int fh_http_client_cancel(struct fh_http_client *c);

#endif
