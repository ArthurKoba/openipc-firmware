#include "fh8626_http_client.h"

#include <errno.h>
#include <string.h>

static void purge(struct fh_http_client *c)
{
    c->head=0;c->count=0;c->queued_bytes=0;c->in_flight=0;
    memset(c->q,0,sizeof(c->q));
}
static void video_loss(struct fh_http_client *c)
{
    c->need_idr=1;
    c->recovery_boundary_seq=c->next_seq;
}
static int fail_client(struct fh_http_client *c,int rc)
{
    c->state=FH_HTTP_FAILED;
    purge(c);
    c->need_idr=1;
    c->header_ready=0;
    return rc ? rc : -EIO;
}
void fh_http_client_init(struct fh_http_client *c,uint64_t id,uint64_t epoch,size_t limit,uint64_t timeout)
{
    if(!c)return;
    memset(c,0,sizeof(*c));
    c->client_id=id;c->epoch=epoch?epoch:1;c->next_seq=1;c->recovery_boundary_seq=1;
    c->send_timeout_ms=timeout;c->byte_limit=limit;c->need_idr=1;c->state=FH_HTTP_READY;
}
int fh_http_client_set_generation(struct fh_http_client *c,uint64_t generation)
{
    if(!c||c->state!=FH_HTTP_READY||!generation)return -EINVAL;
    if(c->codec_generation==generation)return 0;
    purge(c);c->codec_generation=generation;c->header_ready=0;c->need_idr=1;c->recovery_boundary_seq=c->next_seq;
    return 0;
}
int fh_http_client_accept_header(struct fh_http_client *c,uint64_t generation,size_t len)
{
    if(!c||c->state!=FH_HTTP_READY||generation!=c->codec_generation||len==0)return -EINVAL;
    c->header_ready=1;return 0;
}
int fh_http_client_enqueue(struct fh_http_client *c,enum fh_http_packet_type type,uint64_t generation,int key,uint64_t now,const void *bytes,size_t size,uint64_t *seq)
{
    struct fh_http_packet *p;uint64_t id,deadline;
    if(!c||c->state!=FH_HTTP_READY||!bytes||!size||size>FH_HTTP_PACKET_MAX||generation!=c->codec_generation)return -EINVAL;
    if(type==FH_HTTP_VIDEO){if(!c->header_ready)return -EAGAIN;if(c->need_idr&&!key)return -EAGAIN;}
    if(c->count==FH_HTTP_QUEUE_SLOTS||size>c->byte_limit-c->queued_bytes){if(type==FH_HTTP_VIDEO)video_loss(c);return -ENOSPC;}
    if(c->next_seq==UINT64_MAX||UINT64_MAX-now<c->send_timeout_ms)return -EOVERFLOW;
    id=c->next_seq++;deadline=now+c->send_timeout_ms;
    p=&c->q[(c->head+c->count)%FH_HTTP_QUEUE_SLOTS];memset(p,0,sizeof(*p));
    p->seq=id;p->epoch=c->epoch;p->codec_generation=generation;p->deadline_ms=deadline;p->type=type;p->key=key?1:0;p->size=size;memcpy(p->bytes,bytes,size);
    c->count++;c->queued_bytes+=size;if(seq)*seq=id;return 0;
}
int fh_http_client_take(struct fh_http_client *c,uint64_t now,const struct fh_http_packet **packet)
{
    struct fh_http_packet *p;
    if(!c||!packet||c->state!=FH_HTTP_READY||c->in_flight)return -EINVAL;
    if(!c->count)return 0;
    p=&c->q[c->head];
    if(now>=p->deadline_ms)return fail_client(c,-ETIMEDOUT);
    c->in_flight=1;*packet=p;return 1;
}
int fh_http_client_send_result(struct fh_http_client *c,int rc)
{
    struct fh_http_packet *p;
    if(!c||c->state!=FH_HTTP_READY||!c->in_flight||!c->count)return -EINVAL;
    p=&c->q[c->head];
    if(rc)return fail_client(c,rc);
    if(p->type==FH_HTTP_VIDEO&&p->key&&p->seq>=c->recovery_boundary_seq)c->need_idr=0;
    c->queued_bytes-=p->size;c->head=(c->head+1U)%FH_HTTP_QUEUE_SLOTS;c->count--;c->in_flight=0;return 0;
}
int fh_http_client_begin_close(struct fh_http_client *c)
{
    if(!c||c->state!=FH_HTTP_READY)return -EINVAL;
    c->state=FH_HTTP_CLOSING;return 0;
}
int fh_http_client_cancel(struct fh_http_client *c)
{
    if(!c||(c->state!=FH_HTTP_CLOSING&&c->state!=FH_HTTP_FAILED))return -EINVAL;
    purge(c);c->state=FH_HTTP_CLOSED;c->header_ready=0;c->need_idr=1;return 0;
}
