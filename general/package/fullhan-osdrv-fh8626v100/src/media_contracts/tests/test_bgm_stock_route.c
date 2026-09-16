#include "fh8626_bgm_stock_route.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

struct fake_route {
    char writes[8][64];
    unsigned n_write;
    char readback[1024];
    int fail_write_at;
    int fail_read;
    int fail_ioctl_at;
    unsigned long req[8];
    size_t arg_size[8];
    uint32_t arg0[8], arg1[8];
    unsigned n_ioctl;
};

static int wr(void *opaque, const char *path, const char *text, size_t len)
{
    struct fake_route *f = opaque;
    assert(!strcmp(path, FH_ENC_PROC_PATH));
    assert(f->n_write < 8 && len < sizeof(f->writes[0]));
    if (f->fail_write_at > 0 && (int)(f->n_write + 1) == f->fail_write_at) return -EIO;
    memcpy(f->writes[f->n_write], text, len);
    f->writes[f->n_write][len] = 0;
    f->n_write++;
    return 0;
}

static int rd(void *opaque, const char *path, char *buf, size_t cap, size_t *used)
{
    struct fake_route *f = opaque;
    size_t n = strlen(f->readback);
    assert(!strcmp(path, FH_ENC_PROC_PATH));
    if (f->fail_read) return -EIO;
    if (n + 1 > cap) return -EOVERFLOW;
    memcpy(buf, f->readback, n);
    *used = n;
    return 0;
}

static int mi(void *opaque, unsigned long req, void *arg, size_t arg_size)
{
    struct fake_route *f = opaque;
    unsigned i = f->n_ioctl;
    assert(i < 8);
    f->req[i] = req;
    f->arg_size[i] = arg_size;
    f->arg0[i] = *(uint32_t *)arg;
    if (arg_size >= 8) f->arg1[i] = ((uint32_t *)arg)[1];
    f->n_ioctl++;
    if (f->fail_ioctl_at > 0 && (int)f->n_ioctl == f->fail_ioctl_at) return -EIO;
    return 0;
}

int main(void)
{
    struct fake_route f = {0}, bad = {0};
    struct fh_bgm_route_ops ops = {wr, rd, mi, &f};
    struct fh_bgm_route_ops bad_ops = {wr, rd, mi, &bad};
    struct fh_bgm_stock_route r, rbad;
    uint32_t w = 0, h = 0;
    int tex = -1, bgm = -1;

    assert(!fh_bgm_stock_coarse_geometry(1280, 720, &w, &h));
    assert(w == 160 && h == 90);
    assert(!fh_bgm_stock_route_init(&r, &ops, 0));
    assert(fh_bgm_stock_bind(&r) == -EPERM);
    assert(fh_bgm_stock_prepare_encoder_capability(&r, 0, 1) == -EINVAL);

    assert(!fh_bgm_stock_prepare_encoder_capability(&r, 1, 1));
    assert(f.n_write == 2);
    assert(!strcmp(f.writes[0], "texture_0_1\n"));
    assert(!strcmp(f.writes[1], "bgm_0_1\n"));
    assert(fh_bgm_stock_bind(&r) == -EPERM); /* PAE_SET_CONFIG/readback fence */

    snprintf(f.readback, sizeof(f.readback),
             "****<Channel-0 RUN>****\n## channel status:\n## Function:\n[TEXTURE][BGM]\n"
             "****<Channel-1 STOP>****\n## Function:\n");
    assert(!fh_bgm_stock_verify_encoder_capability(&r));
    assert(!fh_bgm_stock_bind(&r));
    assert(f.req[0] == FH_MEDIA_BIND && f.arg_size[0] == 8);
    assert(f.arg0[0] == 5 && f.arg1[0] == 0x11);
    assert(!fh_bgm_stock_unbind(&r));
    assert(f.req[1] == FH_MEDIA_UNBIND_SRC && f.arg_size[1] == 4);
    assert(f.arg0[1] == 5);

    assert(!fh_bgm_stock_prepare_encoder_capability(&r, 0, 0));
    assert(!strcmp(f.writes[2], "texture_0_0\n"));
    assert(!strcmp(f.writes[3], "bgm_0_0\n"));
    snprintf(f.readback, sizeof(f.readback),
             "****<Channel-0 STOP>****\n## Function:\n"
             "****<Channel-1 STOP>****\n## Function:\n[TEXTURE][BGM]\n");
    assert(!fh_bgm_stock_verify_encoder_capability(&r));

    assert(!fh_bgm_parse_enc_effective(
        "****<Channel-0 RUN>****\n## Function:\n[TEXTURE][BGM]\n", 0, &tex, &bgm));
    assert(tex == 1 && bgm == 1);

    /* Negative: proc write success is not acceptance; readback mismatch fails. */
    assert(!fh_bgm_stock_route_init(&rbad, &bad_ops, 0));
    assert(!fh_bgm_stock_prepare_encoder_capability(&rbad, 1, 1));
    snprintf(bad.readback, sizeof(bad.readback),
             "****<Channel-0 RUN>****\n## Function:\n[TEXTURE]\n");
    assert(fh_bgm_stock_verify_encoder_capability(&rbad) == -EIO);
    assert(fh_bgm_stock_route_needs_recovery(&rbad));
    assert(fh_bgm_stock_bind(&rbad) == -EUCLEAN);
    assert(!fh_bgm_stock_route_begin_after_external_recovery(&rbad));


    /* A transient proc read error cannot authorize bind, but can be retried
     * because no new write/ioctl side effect occurs in verify itself. */
    memset(&bad, 0, sizeof(bad));
    assert(!fh_bgm_stock_route_init(&rbad, &bad_ops, 0));
    assert(!fh_bgm_stock_prepare_encoder_capability(&rbad, 1, 1));
    snprintf(bad.readback, sizeof(bad.readback),
             "****<Channel-0 RUN>****\n## Function:\n[TEXTURE][BGM]\n");
    bad.fail_read = 1;
    assert(fh_bgm_stock_verify_encoder_capability(&rbad) == -EIO);
    assert(rbad.state == FH_BGM_ROUTE_PREPARED);
    assert(fh_bgm_stock_bind(&rbad) == -EPERM);
    bad.fail_read = 0;
    assert(!fh_bgm_stock_verify_encoder_capability(&rbad));
    assert(!fh_bgm_stock_bind(&rbad));
    assert(!fh_bgm_stock_unbind(&rbad));

    /* Negative: bind/unbind failures are completion-unknown and poison route. */
    memset(&bad, 0, sizeof(bad));
    assert(!fh_bgm_stock_route_init(&rbad, &bad_ops, 0));
    assert(!fh_bgm_stock_prepare_encoder_capability(&rbad, 1, 1));
    snprintf(bad.readback, sizeof(bad.readback),
             "****<Channel-0 RUN>****\n## Function:\n[TEXTURE][BGM]\n");
    assert(!fh_bgm_stock_verify_encoder_capability(&rbad));
    bad.fail_ioctl_at = 1;
    assert(fh_bgm_stock_bind(&rbad) == -EIO);
    assert(fh_bgm_stock_route_needs_recovery(&rbad));
    assert(fh_bgm_stock_unbind(&rbad) == -EUCLEAN);
    assert(!fh_bgm_stock_route_begin_after_external_recovery(&rbad));
    assert(rbad.state == FH_BGM_ROUTE_FRESH);

    memset(&bad, 0, sizeof(bad));
    assert(!fh_bgm_stock_route_init(&rbad, &bad_ops, 0));
    assert(!fh_bgm_stock_prepare_encoder_capability(&rbad, 1, 1));
    snprintf(bad.readback, sizeof(bad.readback),
             "****<Channel-0 RUN>****\n## Function:\n[TEXTURE][BGM]\n");
    assert(!fh_bgm_stock_verify_encoder_capability(&rbad));
    assert(!fh_bgm_stock_bind(&rbad));
    bad.fail_ioctl_at = 2;
    assert(fh_bgm_stock_unbind(&rbad) == -EIO);
    assert(fh_bgm_stock_route_needs_recovery(&rbad));


    /* Negative: either proc write failure is completion-unknown because a
     * partial/global capability side effect cannot be ruled out. */
    memset(&bad, 0, sizeof(bad));
    assert(!fh_bgm_stock_route_init(&rbad, &bad_ops, 0));
    bad.fail_write_at = 1;
    assert(fh_bgm_stock_prepare_encoder_capability(&rbad, 1, 1) == -EIO);
    assert(fh_bgm_stock_route_needs_recovery(&rbad));
    assert(!fh_bgm_stock_route_begin_after_external_recovery(&rbad));
    bad.fail_write_at = 2;
    assert(fh_bgm_stock_prepare_encoder_capability(&rbad, 1, 1) == -EIO);
    assert(fh_bgm_stock_route_needs_recovery(&rbad));

    puts("BGM capability/readback + exact bind/unbind ABI: PASS");
    return 0;
}
