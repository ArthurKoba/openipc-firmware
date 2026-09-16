#ifndef FH8626_BGM_LINUX_H
#define FH8626_BGM_LINUX_H

#include <stddef.h>
#include <stdint.h>
#include "fh8626_bgm_adapter.h"

/* NEW/RECONFIRMED from stock Apollo MMB wrapper: this userspace ABI is 0x50,
 * not the old owner's unrelated 0x68 request. */
#define FH_VMM_ALLOC     0xC0506D0AUL
#define FH_VMM_FREE_ALL  0x40506D0CUL
#define FH_VMM_REMAP     0xC0506D14UL

struct fh_bgm_vmm_wire {
    uint32_t phys_lo;       /* 0x00: returned physical base low */
    uint32_t phys_hi;       /* 0x04: returned physical base high */
    uint32_t align;         /* 0x08: stock BGM passes 0x400 */
    uint32_t bytes;         /* 0x0c: MEM_QUERY byte count */
    uint32_t field_10;      /* 0x10: unresolved */
    uint32_t user_va;       /* 0x14: returned remapped user VA */
    uint32_t remap_ctl;     /* 0x18: stock sets 0x103 before REMAP */
    char name[16];          /* 0x1c */
    char zone[16];          /* 0x2c */
    uint8_t tail[0x14];     /* 0x3c..0x4f */
};

_Static_assert(sizeof(struct fh_bgm_vmm_wire) == 0x50, "VMM MMB wire ABI");
_Static_assert(offsetof(struct fh_bgm_vmm_wire, phys_lo) == 0x00, "VMM phys low offset");
_Static_assert(offsetof(struct fh_bgm_vmm_wire, phys_hi) == 0x04, "VMM phys high offset");
_Static_assert(offsetof(struct fh_bgm_vmm_wire, align) == 0x08, "VMM align offset");
_Static_assert(offsetof(struct fh_bgm_vmm_wire, bytes) == 0x0c, "VMM bytes offset");
_Static_assert(offsetof(struct fh_bgm_vmm_wire, user_va) == 0x14, "VMM user VA offset");
_Static_assert(offsetof(struct fh_bgm_vmm_wire, remap_ctl) == 0x18, "VMM remap control offset");
_Static_assert(offsetof(struct fh_bgm_vmm_wire, name) == 0x1c, "VMM name offset");
_Static_assert(offsetof(struct fh_bgm_vmm_wire, zone) == 0x2c, "VMM zone offset");

/* Testable POSIX boundary. Callbacks intentionally mimic open/close/ioctl and
 * use errno on -1, so production uses the exact same error translation path. */
struct fh_bgm_linux_sys_ops {
    int (*open_fn)(void *opaque, const char *path, int flags);
    int (*close_fn)(void *opaque, int fd);
    int (*ioctl_fn)(void *opaque, int fd, unsigned long req, void *arg);
    void *opaque;
};

struct fh_bgm_linux {
    int bgm_fd;
    int vmm_fd;
    int vmm_live;
    int vmm_recovery_required;
    int bgm_release_unknown;
    int vmm_release_unknown;
    char bgm_path[128];
    char vmm_path[128];
    struct fh_bgm_vmm_wire vmm_wire;
    struct fh_bgm_linux_sys_ops sys;
};

int fh_bgm_vmm_wire_init(struct fh_bgm_vmm_wire *w, uint32_t bytes,
                         uint32_t align, const char *name, const char *zone);
int fh_bgm_vmm_wire_result(const struct fh_bgm_vmm_wire *w,
                           struct fh_bgm_block *out);

int fh_bgm_linux_open(struct fh_bgm_linux *l, const char *bgm_path,
                      const char *vmm_path);
int fh_bgm_linux_open_with_sys(struct fh_bgm_linux *l, const char *bgm_path,
                               const char *vmm_path,
                               const struct fh_bgm_linux_sys_ops *sys);
/* Only for a backend that has no live BGM allocation. Normal users should let
 * fh_bgm_adapter_destroy() invoke the stronger final_release contract. */
int fh_bgm_linux_close_idle(struct fh_bgm_linux *l);
void fh_bgm_linux_ops(struct fh_bgm_linux *l, struct fh_bgm_ops *ops);

#endif
