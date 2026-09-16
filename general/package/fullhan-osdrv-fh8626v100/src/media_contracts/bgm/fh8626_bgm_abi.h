#ifndef FH8626_BGM_ABI_H
#define FH8626_BGM_ABI_H
#include <stddef.h>
#include <stdint.h>

/* Exact FH8626V100 stock bgm.ko ioctl numbers recovered from target. */
#define FH_BGM_MEM_INIT       0xC0144200UL
#define FH_BGM_MEM_UNINIT     0xC0044201UL
#define FH_BGM_MEM_QUERY      0xC00C4202UL
#define FH_BGM_SET_VI_ATTR    0xC0084203UL
#define FH_BGM_GET_VI_ATTR    0xC0084204UL
#define FH_BGM_ENABLE         0xC0044205UL
#define FH_BGM_DISABLE        0xC0044206UL
#define FH_BGM_SUBMIT         0xC0184207UL
#define FH_BGM_GET_SW_RESULT  0xC0304208UL
#define FH_BGM_GET_HW_RESULT  0xC0984209UL
#define FH_BGM_BKG_REINIT     0xC004421CUL

/* RECONFIRMED dispatcher detail: MEM_UNINIT/ENABLE/DISABLE are encoded with
 * a 4-byte _IOWR size, but the target bgm_ioctrl branches call their handlers
 * directly without copy_from_user and do not consume that word. Passing a null
 * ioctl argument for these three operations is therefore intentional; the
 * encoded size must not be mistaken for a required userspace payload. */

struct fh_bgm_mem_query { uint32_t width, height, bytes; };
struct fh_bgm_mem_init {
    uint32_t phys_base;
    uint32_t user_base;
    uint32_t bytes;
    uint32_t width;
    uint32_t height;
};
struct fh_bgm_vi_attr { uint32_t width, height; };
struct fh_bgm_submit {
    uint32_t width;
    uint32_t height;
    uint32_t y_phys;
    uint32_t opaque0c; /* copied by Apollo; bgm_submit_frm does not consume it */
    uint32_t pts_lo;
    uint32_t pts_hi;
};
/* Keep raw until every pointer/metadata word is proven. words[10:11] are PTS. */
struct fh_bgm_sw_result { uint32_t words[12]; };
struct fh_bgm_hw_result { uint32_t words[38]; };

_Static_assert(sizeof(struct fh_bgm_mem_query)==12,"BGM MEM_QUERY ABI");
_Static_assert(offsetof(struct fh_bgm_mem_query, bytes)==0x08,"BGM MEM_QUERY bytes offset");
_Static_assert(sizeof(struct fh_bgm_mem_init)==20,"BGM MEM_INIT ABI");
_Static_assert(offsetof(struct fh_bgm_mem_init, phys_base)==0x00,"BGM MEM_INIT phys offset");
_Static_assert(offsetof(struct fh_bgm_mem_init, user_base)==0x04,"BGM MEM_INIT user offset");
_Static_assert(offsetof(struct fh_bgm_mem_init, bytes)==0x08,"BGM MEM_INIT bytes offset");
_Static_assert(offsetof(struct fh_bgm_mem_init, width)==0x0c,"BGM MEM_INIT width offset");
_Static_assert(offsetof(struct fh_bgm_mem_init, height)==0x10,"BGM MEM_INIT height offset");
_Static_assert(sizeof(struct fh_bgm_vi_attr)==8,"BGM VI ABI");
_Static_assert(sizeof(struct fh_bgm_submit)==24,"BGM SUBMIT ABI");
_Static_assert(offsetof(struct fh_bgm_submit, y_phys)==0x08,"BGM SUBMIT y offset");
_Static_assert(offsetof(struct fh_bgm_submit, pts_lo)==0x10,"BGM SUBMIT PTS low offset");
_Static_assert(offsetof(struct fh_bgm_submit, pts_hi)==0x14,"BGM SUBMIT PTS high offset");
_Static_assert(sizeof(struct fh_bgm_sw_result)==48,"BGM SW RESULT ABI");
_Static_assert(sizeof(struct fh_bgm_hw_result)==152,"BGM HW RESULT ABI");
#endif
