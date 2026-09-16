#ifndef FH8626_SIDECAR_PUBLISHER_H
#define FH8626_SIDECAR_PUBLISHER_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define FH8626_SIDECAR_WIRE_HEADER_SIZE 32U
#define FH8626_SIDECAR_WIRE_VERSION 1U
#define FH8626_SIDECAR_FLAG_KEYFRAME 0x00000001U

struct fh8626_sidecar_publisher {
    int initialized;
    int listen_fd;
    int client_fd;
    unsigned char *pending;
    size_t max_payload;
    size_t pending_len;
    size_t pending_off;
    uint64_t generation;
    uint64_t frames_queued;
    uint64_t frames_dropped;
    uint64_t clients_accepted;
    uint64_t clients_dropped;
    uint64_t generation_disconnects;
    int waiting_for_idr;
    dev_t socket_dev;
    ino_t socket_ino;
    char socket_path[108];
};

int fh8626_sidecar_publisher_init(struct fh8626_sidecar_publisher *publisher,
    const char *socket_path, size_t max_payload);
void fh8626_sidecar_publisher_close(struct fh8626_sidecar_publisher *publisher);
void fh8626_sidecar_publisher_service(struct fh8626_sidecar_publisher *publisher);
int fh8626_sidecar_publisher_publish(struct fh8626_sidecar_publisher *publisher,
    const void *payload, size_t payload_len, uint64_t pts_us, uint32_t flags);
/* Prepend missing current-epoch parameter sets to an IDR. No stale avcC or
 * SPS rewriting: inputs are Annex-B bytes obtained from this encoder. */
int fh8626_sidecar_publisher_publish_idr(struct fh8626_sidecar_publisher *,
    const void *, size_t, uint64_t, const void *, size_t, const void *, size_t);
void fh8626_sidecar_publisher_bump_generation(struct fh8626_sidecar_publisher *publisher);

#endif
