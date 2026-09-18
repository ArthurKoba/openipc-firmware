#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define LISTEN_PORT 80
#define BACKEND_PORT 18080
#define BUF_SIZE 8192

static volatile sig_atomic_t running = 1;

static void on_signal(int sig) { (void)sig; running = 0; }

static int write_all(int fd, const void *buf, size_t len)
{
    const char *p = buf;
    while (len) {
        ssize_t n = write(fd, p, len);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        p += (size_t)n;
        len -= (size_t)n;
    }
    return 0;
}

static void emit_metrics(int fd)
{
    char body[16384];
    size_t used = 0;
    FILE *f;
    double up = 0.0;
    time_t now = time(NULL);
    unsigned long long user=0,nice=0,sys=0,idle=0,iowait=0,irq=0,softirq=0;
    long hz = sysconf(_SC_CLK_TCK);

#define APPEND(...) do { \
    int n = snprintf(body + used, sizeof(body) - used, __VA_ARGS__); \
    if (n > 0 && (size_t)n < sizeof(body) - used) used += (size_t)n; \
} while (0)

    f = fopen("/proc/uptime", "r");
    if (f) { (void)fscanf(f, "%lf", &up); fclose(f); }

    APPEND("node_time_seconds %lld\n", (long long)now);
    APPEND("node_boot_time_seconds %.0f\n", (double)now - up);

    f = fopen("/proc/stat", "r");
    if (f) {
        char line[512];
        if (fgets(line, sizeof(line), f))
            (void)sscanf(line, "cpu %llu %llu %llu %llu %llu %llu %llu",
                &user,&nice,&sys,&idle,&iowait,&irq,&softirq);
        fclose(f);
    }
    if (hz <= 0) hz = 100;
    APPEND("node_cpu_seconds_total{cpu=\"0\",mode=\"user\"} %.2f\n", (double)user/hz);
    APPEND("node_cpu_seconds_total{cpu=\"0\",mode=\"nice\"} %.2f\n", (double)nice/hz);
    APPEND("node_cpu_seconds_total{cpu=\"0\",mode=\"system\"} %.2f\n", (double)sys/hz);
    APPEND("node_cpu_seconds_total{cpu=\"0\",mode=\"idle\"} %.2f\n", (double)(idle+iowait)/hz);
    APPEND("node_cpu_seconds_total{cpu=\"0\",mode=\"irq\"} %.2f\n", (double)irq/hz);
    APPEND("node_cpu_seconds_total{cpu=\"0\",mode=\"softirq\"} %.2f\n", (double)softirq/hz);

    f = fopen("/proc/meminfo", "r");
    if (f) {
        char key[64], unit[16];
        unsigned long long val;
        while (fscanf(f, "%63s %llu %15s", key, &val, unit) >= 2) {
            size_t n = strlen(key);
            if (n && key[n-1] == ':') key[n-1] = 0;
            if (!strcmp(key,"MemTotal") || !strcmp(key,"MemFree") ||
                !strcmp(key,"MemAvailable") || !strcmp(key,"Buffers") ||
                !strcmp(key,"Cached") || !strcmp(key,"SReclaimable") ||
                !strcmp(key,"SwapTotal") || !strcmp(key,"SwapFree"))
                APPEND("node_memory_%s_bytes %llu\n", key, val * 1024ULL);
        }
        fclose(f);
    }

    f = fopen("/proc/net/dev", "r");
    if (f) {
        char line[512];
        int skip = 2;
        while (fgets(line, sizeof(line), f)) {
            char dev[64];
            unsigned long long rx=0, tx=0;
            char *colon;
            if (skip) { skip--; continue; }
            colon = strchr(line, ':');
            if (!colon) continue;
            *colon = 0;
            if (sscanf(line, " %63s", dev) != 1) continue;
            if (sscanf(colon + 1,
                " %llu %*u %*u %*u %*u %*u %*u %*u %llu",
                &rx, &tx) != 2) continue;
            if (!strcmp(dev, "lo")) continue;
            APPEND("node_network_receive_bytes_total{device=\"%s\"} %llu\n", dev, rx);
            APPEND("node_network_transmit_bytes_total{device=\"%s\"} %llu\n", dev, tx);
        }
        fclose(f);
    }

    {
        char hdr[256];
        int n = snprintf(hdr, sizeof(hdr),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain; version=0.0.4\r\n"
            "Content-Length: %zu\r\n"
            "Connection: close\r\n\r\n", used);
        if (n > 0) {
            (void)write_all(fd, hdr, (size_t)n);
            (void)write_all(fd, body, used);
        }
    }
#undef APPEND
}

static int is_metrics_request(const char *buf, size_t len)
{
    const char *sp, *end;
    if (len < 5 || memcmp(buf, "GET ", 4)) return 0;
    sp = buf + 4;
    end = memchr(sp, ' ', len - 4);
    if (!end) return 0;
    return (size_t)(end - sp) == 8 && !memcmp(sp, "/metrics", 8);
}

static int connect_backend(void)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in sa;
    if (fd < 0) return -1;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(BACKEND_PORT);
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static void tunnel(int client, const char *initial, size_t initial_len)
{
    int backend = connect_backend();
    struct pollfd pfd[2];
    char buf[BUF_SIZE];

    if (backend < 0) {
        static const char unavailable[] =
            "HTTP/1.1 502 Bad Gateway\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
        (void)write_all(client, unavailable, sizeof(unavailable)-1);
        return;
    }
    if (write_all(backend, initial, initial_len) < 0) {
        close(backend);
        return;
    }

    pfd[0].fd = client; pfd[0].events = POLLIN;
    pfd[1].fd = backend; pfd[1].events = POLLIN;
    while (running) {
        int rc = poll(pfd, 2, -1);
        if (rc < 0) {
            if (errno == EINTR) continue;
            break;
        }
        for (int i = 0; i < 2; ++i) {
            if (pfd[i].revents & (POLLERR|POLLHUP|POLLNVAL)) goto done;
            if (pfd[i].revents & POLLIN) {
                ssize_t n = read(pfd[i].fd, buf, sizeof(buf));
                int out = pfd[1-i].fd;
                if (n <= 0 || write_all(out, buf, (size_t)n) < 0) goto done;
            }
        }
    }
done:
    close(backend);
}

static void handle_client(int client)
{
    char buf[BUF_SIZE];
    ssize_t n = read(client, buf, sizeof(buf));
    if (n <= 0) return;
    if (is_metrics_request(buf, (size_t)n))
        emit_metrics(client);
    else
        tunnel(client, buf, (size_t)n);
}

int main(void)
{
    int fd, one = 1;
    struct sockaddr_in sa;

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(LISTEN_PORT);
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0 || listen(fd, 16) < 0) {
        perror("fh8626-majestic-http");
        close(fd);
        return 1;
    }

    while (running) {
        int c = accept(fd, NULL, NULL);
        if (c < 0) {
            if (errno == EINTR) continue;
            break;
        }
        handle_client(c);
        close(c);
    }
    close(fd);
    return 0;
}
