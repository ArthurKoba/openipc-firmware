#define _GNU_SOURCE
#include "fh8626_sidecar_publisher.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

static void put_be32(unsigned char *p, uint32_t value)
{
    p[0] = (unsigned char)(value >> 24);
    p[1] = (unsigned char)(value >> 16);
    p[2] = (unsigned char)(value >> 8);
    p[3] = (unsigned char)value;
}

static void put_be64(unsigned char *p, uint64_t value)
{
    p[0] = (unsigned char)(value >> 56);
    p[1] = (unsigned char)(value >> 48);
    p[2] = (unsigned char)(value >> 40);
    p[3] = (unsigned char)(value >> 32);
    p[4] = (unsigned char)(value >> 24);
    p[5] = (unsigned char)(value >> 16);
    p[6] = (unsigned char)(value >> 8);
    p[7] = (unsigned char)value;
}

static int set_fd_flags(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    int fdflags;

    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
        return -1;

    fdflags = fcntl(fd, F_GETFD, 0);
    if (fdflags < 0 || fcntl(fd, F_SETFD, fdflags | FD_CLOEXEC) < 0)
        return -1;

    return 0;
}

static int path_matches_owned_socket(const char *path, dev_t dev, ino_t ino)
{
    struct stat st;

    if (!path || !*path || lstat(path, &st) != 0)
        return 0;

    return S_ISSOCK(st.st_mode) && st.st_dev == dev && st.st_ino == ino;
}

static void drop_client(struct fh8626_sidecar_publisher *publisher)
{
    if (publisher->client_fd >= 0) {
        close(publisher->client_fd);
        publisher->clients_dropped++;
    }

    publisher->client_fd = -1;
    publisher->pending_len = 0;
    publisher->pending_off = 0;
}

int fh8626_sidecar_publisher_init(struct fh8626_sidecar_publisher *publisher,
    const char *socket_path, size_t max_payload)
{
    struct sockaddr_un addr;
    struct stat st;
    unsigned char *pending = NULL;
    dev_t bound_dev = 0;
    ino_t bound_ino = 0;
    size_t path_len;
    int fd = -1;
    int bound = 0;
    int have_bound_identity = 0;
    int saved_errno;

    if (!publisher || !socket_path || !*socket_path || max_payload == 0 ||
        max_payload > UINT32_MAX) {
        errno = EINVAL;
        return -1;
    }

    path_len = strlen(socket_path);
    if (path_len >= sizeof(addr.sun_path)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    if (max_payload > SIZE_MAX - FH8626_SIDECAR_WIRE_HEADER_SIZE) {
        errno = EOVERFLOW;
        return -1;
    }

    memset(publisher, 0, sizeof(*publisher));
    publisher->listen_fd = -1;
    publisher->client_fd = -1;

    pending = malloc(FH8626_SIDECAR_WIRE_HEADER_SIZE + max_payload);
    if (!pending)
        return -1;

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        goto fail;

    if (set_fd_flags(fd) != 0)
        goto fail;

    if (lstat(socket_path, &st) == 0) {
        errno = S_ISSOCK(st.st_mode) ? EADDRINUSE : EEXIST;
        goto fail;
    }
    if (errno != ENOENT)
        goto fail;

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    memcpy(addr.sun_path, socket_path, path_len + 1);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
        goto fail;
    bound = 1;

    if (lstat(socket_path, &st) != 0)
        goto fail;
    if (!S_ISSOCK(st.st_mode)) {
        errno = EIO;
        goto fail;
    }
    bound_dev = st.st_dev;
    bound_ino = st.st_ino;
    have_bound_identity = 1;

    if (listen(fd, 1) != 0)
        goto fail;

    memset(publisher, 0, sizeof(*publisher));
    publisher->initialized = 1;
    publisher->listen_fd = fd;
    publisher->client_fd = -1;
    publisher->pending = pending;
    publisher->max_payload = max_payload;
    publisher->generation = 1;
    publisher->socket_dev = bound_dev;
    publisher->socket_ino = bound_ino;
    memcpy(publisher->socket_path, socket_path, path_len + 1);
    return 0;

fail:
    saved_errno = errno;
    if (fd >= 0)
        close(fd);
    if (bound && have_bound_identity &&
        path_matches_owned_socket(socket_path, bound_dev, bound_ino))
        unlink(socket_path);
    free(pending);
    memset(publisher, 0, sizeof(*publisher));
    publisher->listen_fd = -1;
    publisher->client_fd = -1;
    errno = saved_errno;
    return -1;
}

void fh8626_sidecar_publisher_close(struct fh8626_sidecar_publisher *publisher)
{
    char socket_path[sizeof(publisher->socket_path)];
    dev_t socket_dev;
    ino_t socket_ino;

    if (!publisher || !publisher->initialized)
        return;

    memcpy(socket_path, publisher->socket_path, sizeof(socket_path));
    socket_path[sizeof(socket_path) - 1] = '\0';
    socket_dev = publisher->socket_dev;
    socket_ino = publisher->socket_ino;

    drop_client(publisher);
    if (publisher->listen_fd >= 0)
        close(publisher->listen_fd);

    free(publisher->pending);

    memset(publisher, 0, sizeof(*publisher));
    publisher->listen_fd = -1;
    publisher->client_fd = -1;

    if (path_matches_owned_socket(socket_path, socket_dev, socket_ino))
        unlink(socket_path);
}

void fh8626_sidecar_publisher_service(struct fh8626_sidecar_publisher *publisher)
{
    if (!publisher || !publisher->initialized || publisher->listen_fd < 0)
        return;

    if (publisher->client_fd < 0) {
        int fd = accept(publisher->listen_fd, NULL, NULL);

        if (fd >= 0) {
            if (set_fd_flags(fd) != 0) {
                close(fd);
            } else {
                publisher->client_fd = fd;
                publisher->clients_accepted++;
                publisher->waiting_for_idr = 1;
            }
        } else if (errno != EAGAIN && errno != EWOULDBLOCK &&
            errno != EINTR) {
            return;
        }
    }

    while (publisher->client_fd >= 0 &&
        publisher->pending_off < publisher->pending_len) {
        ssize_t sent = send(publisher->client_fd,
            publisher->pending + publisher->pending_off,
            publisher->pending_len - publisher->pending_off,
            MSG_DONTWAIT | MSG_NOSIGNAL);

        if (sent > 0) {
            publisher->pending_off += (size_t)sent;
            continue;
        }

        if (sent < 0 && errno == EINTR)
            continue;
        if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return;

        drop_client(publisher);
        return;
    }

    if (publisher->pending_off == publisher->pending_len) {
        publisher->pending_len = 0;
        publisher->pending_off = 0;
    }
}

int fh8626_sidecar_publisher_publish(struct fh8626_sidecar_publisher *publisher,
    const void *payload, size_t payload_len, uint64_t pts_us, uint32_t flags)
{
    unsigned char *header;

    if (!publisher || !publisher->initialized || !payload || payload_len == 0) {
        errno = EINVAL;
        return -1;
    }

    if (payload_len > publisher->max_payload || payload_len > UINT32_MAX) {
        errno = EMSGSIZE;
        return -1;
    }

    fh8626_sidecar_publisher_service(publisher);
    if (publisher->client_fd < 0)
        return 0;

    if (publisher->waiting_for_idr &&
        !(flags & FH8626_SIDECAR_FLAG_KEYFRAME)) {
        publisher->frames_dropped++;
        return 0;
    }

    if (publisher->pending_len != 0) {
        publisher->frames_dropped++;
        publisher->generation = publisher->generation == UINT64_MAX ?
            1 : publisher->generation + 1;
        publisher->generation_disconnects++;
        publisher->waiting_for_idr = 1;
        drop_client(publisher);
        return 0;
    }

    header = publisher->pending;
    header[0] = 'F';
    header[1] = 'H';
    header[2] = '8';
    header[3] = '6';
    put_be32(header + 4, FH8626_SIDECAR_WIRE_VERSION);
    put_be32(header + 8, (uint32_t)payload_len);
    put_be32(header + 12, flags);
    put_be64(header + 16, pts_us);
    put_be64(header + 24, publisher->generation);
    memcpy(header + FH8626_SIDECAR_WIRE_HEADER_SIZE, payload, payload_len);

    publisher->pending_len = FH8626_SIDECAR_WIRE_HEADER_SIZE + payload_len;
    publisher->pending_off = 0;
    publisher->frames_queued++;
    if (flags & FH8626_SIDECAR_FLAG_KEYFRAME)
        publisher->waiting_for_idr = 0;
    fh8626_sidecar_publisher_service(publisher);
    return 1;
}

int fh8626_sidecar_publisher_publish_idr(struct fh8626_sidecar_publisher *p,
    const void *au,size_t n,uint64_t pts,const void *sps,size_t ns,
    const void *pps,size_t np)
{
    unsigned char *joined;
    size_t total;
    int rc;
    if(!p || !au || (ns && !sps) || (np && !pps))return -EINVAL;
    if(n>p->max_payload || ns>p->max_payload-n ||
       np>p->max_payload-n-ns)return -EMSGSIZE;
    if(!ns && !np)return fh8626_sidecar_publisher_publish(p,au,n,pts,
        FH8626_SIDECAR_FLAG_KEYFRAME);
    total=ns+np+n;
    joined=malloc(total);
    if(!joined)return -ENOMEM;
    if(ns)memcpy(joined,sps,ns);
    if(np)memcpy(joined+ns,pps,np);
    memcpy(joined+ns+np,au,n);
    rc=fh8626_sidecar_publisher_publish(p,joined,total,pts,FH8626_SIDECAR_FLAG_KEYFRAME);
    free(joined);return rc;
}

void fh8626_sidecar_publisher_bump_generation(struct fh8626_sidecar_publisher *publisher)
{
    int partial_frame;
    int generation_wrap;

    if (!publisher || !publisher->initialized)
        return;

    partial_frame = publisher->pending_len != 0 && publisher->pending_off != 0;
    generation_wrap = publisher->generation == UINT64_MAX;

    if (publisher->pending_len != 0)
        publisher->frames_dropped++;

    publisher->generation = generation_wrap ? 1 : publisher->generation + 1;

    if (publisher->client_fd >= 0 && (partial_frame || generation_wrap)) {
        publisher->generation_disconnects++;
        drop_client(publisher);
        return;
    }

    publisher->pending_len = 0;
    publisher->pending_off = 0;
}
