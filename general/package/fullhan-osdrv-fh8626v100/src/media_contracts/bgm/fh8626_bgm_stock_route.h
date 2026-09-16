#ifndef FH8626_BGM_STOCK_ROUTE_H
#define FH8626_BGM_STOCK_ROUTE_H

#include <stddef.h>
#include <stdint.h>

struct fh_media_bind_wire { uint32_t source; uint32_t destination; };
struct fh_media_unbind_src_wire { uint32_t source; };
_Static_assert(sizeof(struct fh_media_bind_wire) == 8, "MEDIA_BIND wire ABI");
_Static_assert(offsetof(struct fh_media_bind_wire, source) == 0x00, "MEDIA_BIND source offset");
_Static_assert(offsetof(struct fh_media_bind_wire, destination) == 0x04, "MEDIA_BIND destination offset");
_Static_assert(sizeof(struct fh_media_unbind_src_wire) == 4, "MEDIA_UNBIND_SRC wire ABI");
_Static_assert(offsetof(struct fh_media_unbind_src_wire, source) == 0x00, "MEDIA_UNBIND_SRC source offset");

/* RECONFIRMED: stock bind is a two-u32 {source,destination} payload. */
#define FH_MEDIA_BIND             0xC0084D00UL
/* NEW: media_ioctl -> media_unbind_src copies one u32 and unbinds by source. */
#define FH_MEDIA_UNBIND_SRC       0xC0044D02UL
#define FH_MEDIA_OBJ_VPU_BGM_SRC  5u
#define FH_MEDIA_OBJ_BGM          0x11u
#define FH_ENC_PROC_PATH          "/proc/driver/enc"

struct fh_bgm_route_ops {
    int (*write_text)(void *opaque, const char *path, const char *text, size_t len);
    int (*read_text)(void *opaque, const char *path, char *buf, size_t cap, size_t *used);
    int (*media_ioctl)(void *opaque, unsigned long req, void *arg, size_t arg_size);
    void *opaque;
};

enum fh_bgm_route_state {
    FH_BGM_ROUTE_FRESH = 0,
    FH_BGM_ROUTE_PREPARED,
    FH_BGM_ROUTE_VERIFIED,
    FH_BGM_ROUTE_BOUND,
    FH_BGM_ROUTE_RECOVERY_REQUIRED
};

struct fh_bgm_stock_route {
    struct fh_bgm_route_ops ops;
    unsigned channel;
    int requested_texture;
    int requested_bgm;
    int effective_texture;
    int effective_bgm;
    enum fh_bgm_route_state state;
};

/* Stock Apollo expression is ((native + 15) >> 3) & ~1, i.e. two cells per
 * 16 pixels. 1280x720 -> 160x90. */
int fh_bgm_stock_coarse_geometry(uint32_t native_width, uint32_t native_height,
                                 uint32_t *coarse_width, uint32_t *coarse_height);
int fh_bgm_stock_route_init(struct fh_bgm_stock_route *r,
                            const struct fh_bgm_route_ops *ops,
                            unsigned channel);

/* NEW lifecycle:
 *   prepare() MUST run before the cold PAE_SET_CONFIG snapshot;
 *   verify() MUST run after PAE_SET_CONFIG and before native bind.
 * BGM requires TEXTURE. Both states are written explicitly, including OFF. */
int fh_bgm_stock_prepare_encoder_capability(struct fh_bgm_stock_route *r,
                                            int texture_enable,
                                            int bgm_enable);
int fh_bgm_stock_verify_encoder_capability(struct fh_bgm_stock_route *r);
int fh_bgm_stock_bind(struct fh_bgm_stock_route *r);
int fh_bgm_stock_unbind(struct fh_bgm_stock_route *r);
int fh_bgm_stock_route_needs_recovery(const struct fh_bgm_stock_route *r);
/* Software epoch marker only; caller invokes this after an external media/encoder
 * restart has resolved an unknown bind/unbind/proc-write completion. */
int fh_bgm_stock_route_begin_after_external_recovery(struct fh_bgm_stock_route *r);

/* Pure parser used by tests and non-Linux integrations. */
int fh_bgm_parse_enc_effective(const char *text, unsigned channel,
                               int *texture_enabled, int *bgm_enabled);

/* Linux helpers. opaque for media_ioctl points to int fd; write/read ignore it. */
int fh_bgm_linux_write_text(void *opaque, const char *path, const char *text, size_t len);
int fh_bgm_linux_read_text(void *opaque, const char *path, char *buf, size_t cap, size_t *used);
int fh_bgm_linux_media_ioctl(void *opaque, unsigned long req, void *arg, size_t arg_size);

#endif
