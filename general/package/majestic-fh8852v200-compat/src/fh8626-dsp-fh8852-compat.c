#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

/*
 * FH8852V200 libdsp compatibility facade for FH8626V100 Majestic bring-up.
 *
 * The FH8852 VPSS userspace ABI uses the same wire sizes for several records
 * but a different ioctl family (0x56xx instead of FH8626 0x69xx). This source
 * layer translates only contracts that are already recovered on FH8626.
 *
 * The active Majestic-facing media surface is translated from recovered
 * FH8852 public contracts onto FH8626 native contracts: multi-channel H.264
 * attributes/RC/stream ownership, VPSS, JPEG/MJPEG, motion/YCmean and OSD
 * GraphV2. Remaining SDK-only exports are explicit -ENOTSUP boundaries and
 * are excluded from the advertised FH8626 capability surface.
 */

#define FH8626_VMM_ALLOC          0xC0686D0AUL
#define FH8626_MEDIA_BIND         0xC0084D00UL
#define FH8626_MEDIA_UNBIND_DST   0xC0044D01UL
#define FH8626_MEDIA_UNBIND_SRC   0xC0044D02UL
#define FH8626_MEDIA_STREAM_NB    0xC1704D05UL
#define FH8626_MEDIA_STREAM       0xC1704D06UL
#define FH8626_MEDIA_STREAM_KIND  4u
#define FH8626_MEDIA_DESC_WORDS   92u
#define FH8852_PUBLIC_STREAM_SIZE 0x140u
#define FH8626_VPU_MEM_QUERY      0xC0046942UL
#define FH8626_VPU_SYS_MEM_INIT   0xC00C6940UL
#define FH8626_VPU_SET_VI_ATTR    0xC0F46946UL
#define FH8626_VPU_CHN_MEM_QUERY  0xC0106943UL
#define FH8626_VPU_SET_CHN_MEM    0xC0186944UL
#define FH8626_VPU_SET_CHN_CFG    0xC00C6948UL
#define FH8626_VPU_OPEN_CHN       0xC004694FUL
#define FH8626_VPU_ENABLE         0xC004694DUL
#define FH8626_VPU_DISABLE        0xC004694EUL
#define FH8626_VPU_CLOSE_CHN      0xC0046950UL
#define FH8626_VPU_SET_FRAMECTRL  0xC0086954UL
#define FH8626_VPU_GET_FRAMECTRL  0xC0086955UL
#define FH8626_VPU_GET_MEAN       0xC01C6959UL
#define FH8626_VPU_GET_CPY        0xC0206961UL
#define FH8626_VPU_YCMEAN_OPEN    0xC0046964UL
#define FH8626_VPU_YCMEAN_CLOSE   0xC0046965UL
#define FH8626_VPU_SET_YCMEANMODE 0xC004696CUL
#define FH8626_VPU_GET_VI_ATTR    0xC00C6947UL
#define FH8626_VPU_GET_CHN_CFG    0xC00C6949UL
#define FH8626_VPU_SET_LOGOV2     0xC448696DUL
#define FH8626_VPU_GET_LOGOV2     0xC448696EUL
#define FH8852_GRAPHV2_WORDS      273u
#define FH8626_LOGOV2_WORDS       274u
#define FH8626_LOGOV2_PLANE_WORDS 128u
#define FH8626_LOGOV2_GLOBAL_SLOTS  2u
#define FH8626_LOGOV2_CHN_SET_SLOTS 4u
#define FH8626_LOGOV2_CHN_GET_SLOTS 5u

#define FH8626_PAE_SYS_QUERY      0xC0045002UL
#define FH8626_PAE_SYS_INIT       0xC00C5000UL
#define FH8626_PAE_ENC_MEM_SIZE   0xC0145003UL
#define FH8626_PAE_ENC_MEM_INIT   0xC01C5004UL
#define FH8626_PAE_SET_CONFIG     0xC02C5006UL
#define FH8626_PAE_GET_CONFIG     0xC02C5007UL
#define FH8626_PAE_START_RECV     0xC0045008UL
#define FH8626_PAE_STOP_RECV      0xC0045009UL
#define FH8626_PAE_STREAM_STEP    0xC0045011UL
#define FH8626_PAE_FORCE_I        0xC0045014UL
#define FH8626_PAE_SET_RC         0xC054502FUL
#define FH8626_PAE_GET_RC         0xC0545030UL

#define FH8626_JPEG_MEM_INIT      0xC0184A00UL
#define FH8626_JPEG_MEM_UNINIT    0xC0184A01UL
#define FH8626_JPEG_MEM_QUERY     0xC0104A02UL
#define FH8626_JPEG_SET_CHN_CFG   0xC0104A03UL
#define FH8626_JPEG_GET_CHN_CFG   0xC0104A04UL
#define FH8626_MJPEG_SET_CHN_CFG  0xC0344A05UL
#define FH8626_MJPEG_GET_CHN_CFG  0xC0344A06UL
#define FH8626_MJPEG_SET_RC       0xC01C4A07UL
#define FH8626_MJPEG_GET_RC       0xC01C4A08UL
#define FH8626_JPEG_START         0xC0044A09UL
#define FH8626_JPEG_STOP          0xC0044A0AUL
#define FH8626_JPEG_SET_ROTATE    0xC0084A0DUL
#define FH8626_JPEG_GET_ROTATE    0xC0084A0EUL
#define FH8626_JPEG_SUBMIT_FRAME  0xC0304A0FUL
#define FH8626_JPEG_RELEASE       0xC0044A10UL
#define FH8626_JPEG_HW_AVG_TIME   0xC0104A11UL
#define FH8626_MJPEG_SET_DROP     0xC0244A0BUL
#define FH8626_MJPEG_GET_DROP     0xC0244A0CUL

#define FH8626_JPEG_MODE_SNAPSHOT 1u
#define FH8626_JPEG_MODE_MJPEG    2u
#define FH8626_JPEG_CHANNELS      9u
#define FH8852_RAW_STREAM_SIZE    0x1A0u

#define FH8626_WIDTH              1280u
#define FH8626_HEIGHT             720u
#define FH8626_FPS_PACKED         0x00010019u

struct mem3 {
    uint32_t phys, virt, size;
};

struct vpu_query {
    uint32_t chn, width, height, size;
};

struct vpu_mem {
    uint32_t chn, phys, virt, size, width, height;
};

struct channel_cfg {
    uint32_t chn, width, height;
};

struct pae_mem_query {
    uint32_t chn, size, width, height, refmode;
};

struct pae_mem {
    uint32_t chn, phys, virt, size, width, height, refmode;
};

struct pae_cfg {
    uint32_t chn, width, height, field0c, profile, qp, fps, mode;
    uint32_t field20, field24, field28;
};

struct pae_rc {
    uint32_t chn, rc_mode, frame_rate_packed, mode2_qp_a, mode2_qp_b;
    uint32_t init_qp, bitrate_or_rate;
    uint32_t i_min_qp, i_max_qp, p_min_qp, p_max_qp;
    uint32_t i_proportion, p_proportion, fluctuate_level;
    int32_t ip_qp_delta;
    uint32_t i_target_limit_bits, max_rate_percent, still_rate_percent;
    uint32_t max_still_qp, additional_rate_bits, extra_qp_parameter;
};

struct jpeg_query {
    uint32_t mode, width, height, size;
};

struct jpeg_mem {
    uint32_t mode, phys, virt, size, width, height;
};

struct jpeg_snapshot_cfg {
    uint32_t speed_or_mode, mode, quality_hw, rotate;
};

struct jpeg_mjpeg_cfg {
    uint32_t word[13];
};

struct jpeg_rc {
    uint32_t word[7];
};

struct jpeg_rotate {
    uint32_t mode, rotate;
};

_Static_assert(sizeof(struct jpeg_query) == 0x10, "FH8626 JPEG query wire size");
_Static_assert(sizeof(struct jpeg_mem) == 0x18, "FH8626 JPEG memory wire size");
_Static_assert(sizeof(struct jpeg_snapshot_cfg) == 0x10, "FH8626 JPEG cfg wire size");
_Static_assert(sizeof(struct jpeg_mjpeg_cfg) == 0x34, "FH8626 MJPEG cfg wire size");
_Static_assert(sizeof(struct jpeg_rc) == 0x1c, "FH8626 MJPEG RC wire size");

_Static_assert(sizeof(struct pae_rc) == 0x54, "FH8626 PAE RC wire size");

_Static_assert(sizeof(struct mem3) == 0x0c, "FH8626 mem3 wire size");
_Static_assert(sizeof(struct vpu_query) == 0x10, "FH8626 VPU query wire size");
_Static_assert(sizeof(struct vpu_mem) == 0x18, "FH8626 VPU memory wire size");
_Static_assert(sizeof(struct channel_cfg) == 0x0c, "FH8626 VPU channel cfg wire size");
_Static_assert(sizeof(struct pae_mem_query) == 0x14, "FH8626 PAE query wire size");
_Static_assert(sizeof(struct pae_mem) == 0x1c, "FH8626 PAE memory wire size");
_Static_assert(sizeof(struct pae_cfg) == 0x2c, "FH8626 PAE config wire size");

static int isp_fd = -1;
static int media_fd = -1;
static int pae_fd = -1;
static int vmm_fd = -1;
static int jpeg_fd = -1;
static struct mem3 vpu_sys;
#define FH8626_VPU_CHANNELS 5u
#define FH8626_VENC_CHANNELS 8u
#define FH8626_PRODUCT_VENC_CHANNELS 3u

static struct mem3 vpu_chn[FH8626_VPU_CHANNELS];
static struct mem3 pae_sys;
static struct mem3 pae_chn[FH8626_VENC_CHANNELS];
static uint32_t venc_support_type[FH8626_VENC_CHANNELS];
static uint32_t venc_capacity_width[FH8626_VENC_CHANNELS];
static uint32_t venc_capacity_height[FH8626_VENC_CHANNELS];
static int pae_configured[FH8626_VENC_CHANNELS];
static uint32_t media_bound_mask;
static int stream_lease_held;
static uint32_t stream_lease_channel;
static uint32_t stream_desc[FH8626_MEDIA_DESC_WORDS];

static uint32_t jpeg_mode[FH8626_JPEG_CHANNELS];
static struct mem3 jpeg_mem_state[FH8626_JPEG_CHANNELS];
static uint32_t jpeg_width[FH8626_JPEG_CHANNELS];
static uint32_t jpeg_height[FH8626_JPEG_CHANNELS];
static uint32_t jpeg_attr[FH8626_JPEG_CHANNELS][38];
static uint32_t jpeg_rc_attr[FH8626_JPEG_CHANNELS][17];
static uint8_t jpeg_attr_valid[FH8626_JPEG_CHANNELS];
static uint8_t jpeg_rc_valid[FH8626_JPEG_CHANNELS];
static uint32_t jpeg_snapshot_channel = UINT32_MAX;
static uint32_t jpeg_mjpeg_channel = UINT32_MAX;

static const uint32_t jpeg_quality_lut[10] = {
    0x10u, 0x20u, 0x30u, 0x50u, 0x70u,
    0x90u, 0x110u, 0x130u, 0x150u, 0x170u,
};

static int env_true(const char *name)
{
    const char *v = getenv(name);
    return v && v[0] && strcmp(v, "0");
}

static int strict_stub(const char *name)
{
    if (env_true("FH8626_MAJESTIC_TRACE"))
        fprintf(stderr, "fh8626-dsp-compat: discovery boundary %s\n", name);
    return env_true("FH8626_MAJESTIC_STUB_OK") ? 0 : -ENOSYS;
}

static int unsupported_feature(const char *name)
{
    if (env_true("FH8626_MAJESTIC_TRACE"))
        fprintf(stderr, "fh8626-dsp-compat: unsupported/unreachable SDK API %s\n", name);
    return -ENOTSUP;
}

static int call_ioctl(int fd, unsigned long request, void *arg)
{
    int rc;
    errno = 0;
    rc = ioctl(fd, request, arg);
    if (!rc)
        return 0;
    return rc == -1 && errno ? -errno : -EIO;
}

static int open_one(int *fd, const char *path)
{
    if (*fd >= 0)
        return 0;
    *fd = open(path, O_RDWR | O_CLOEXEC);
    return *fd >= 0 ? 0 : -errno;
}

static int open_native(void)
{
    int rc;
    if ((rc = open_one(&isp_fd, "/dev/isp")) ||
        (rc = open_one(&media_fd, "/dev/media_process")) ||
        (rc = open_one(&pae_fd, "/dev/pae")) ||
        (rc = open_one(&vmm_fd, "/dev/vmm_userdev")))
        return rc;
    return 0;
}


static int open_jpeg(void)
{
    return open_one(&jpeg_fd, "/dev/jpeg");
}

static void *map_phys(uint32_t phys, uint32_t size)
{
#if defined(__arm__)
    return (void *)(intptr_t)syscall(192, NULL, size, PROT_READ | PROT_WRITE,
                                     MAP_SHARED, vmm_fd, phys >> 12);
#else
    return mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, vmm_fd, phys);
#endif
}


static int alloc_vmm(const char *name, uint32_t need, struct mem3 *mem)
{
    uint8_t request[104];
    void *mapped;
    int rc;

    if (!name || !need || !mem || mem->phys)
        return -EINVAL;
    if ((rc = open_native()))
        return rc;

    mem->size = (need + 4095u) & ~4095u;
    memset(request, 0, sizeof(request));
    memcpy(request + 8, &(uint32_t){4096u}, 4);
    memcpy(request + 12, &mem->size, 4);
    strncpy((char *)request + 28, name, 15);
    strncpy((char *)request + 44, "anonymous", 15);
    rc = call_ioctl(vmm_fd, FH8626_VMM_ALLOC, request);
    if (rc) {
        memset(mem, 0, sizeof(*mem));
        return rc;
    }
    memcpy(&mem->phys, request, 4);
    mapped = map_phys(mem->phys, mem->size);
    if (mapped == MAP_FAILED) {
        memset(mem, 0, sizeof(*mem));
        return -errno;
    }
    memset(mapped, 0, mem->size);
    mem->virt = (uint32_t)(uintptr_t)mapped;
    return 0;
}

static void trace_words(const char *name, uint32_t chn, const void *ptr,
                        unsigned words)
{
    const uint32_t *p = ptr;
    unsigned i;

    if (!env_true("FH8626_MAJESTIC_TRACE"))
        return;
    fprintf(stderr, "fh8626-dsp-compat: %s chn=%u arg=%p", name, chn, ptr);
    if (p) {
        for (i = 0; i < words; ++i)
            fprintf(stderr, " w%u=%08x", i, p[i]);
    }
    fputc('\n', stderr);
}


/* --- generic Fullhan stream query --- */

static int fh8626_raw_stream_query(void *raw, int blocking)
{
    uint32_t native[FH8626_MEDIA_DESC_WORDS];
    uint32_t mask;
    int rc;

    if (!raw)
        return -EINVAL;
    if ((rc = open_native()))
        return rc;

    mask = ((uint32_t *)raw)[0];
    if (!(mask & 0x0fu))
        return -EINVAL;

    memset(native, 0, sizeof(native));
    native[0] = mask;
    rc = call_ioctl(media_fd,
        blocking ? FH8626_MEDIA_STREAM : FH8626_MEDIA_STREAM_NB, native);
    if (rc)
        return rc;

    /*
     * FH8852 raw records are 0x1a0 bytes; FH8626 records are 0x170.
     * The leading stream mask/kind and the copied inner records keep the same
     * semantics for the fields consumed by our VENC/JPEG public translators.
     */
    memset(raw, 0, FH8852_RAW_STREAM_SIZE);
    memcpy(raw, native, sizeof(native));
    return 0;
}

int _fh_sys_get_stream(void *raw)
{
    return fh8626_raw_stream_query(raw, 0);
}

int _fh_sys_get_stream_block(void *raw)
{
    return fh8626_raw_stream_query(raw, 1);
}

/* --- SYS --- */

int FH_SYS_Init(void)
{
    return open_native();
}

int FH_SYS_Init_Pre(void) { return FH_SYS_Init(); }
int FH_SYS_Init_Post(void) { return 0; }

int FH_SYS_Exit(void)
{
    return 0;
}

int FH_SYS_BindVpu2Enc(uint32_t vpu_chn, uint32_t venc_chn)
{
    uint32_t bind[2];
    int rc;

    if (vpu_chn >= FH8626_VPU_CHANNELS || venc_chn >= FH8626_VENC_CHANNELS)
        return -EINVAL;

    /*
     * Recovered FH8852/FH8626 public bind contract:
     * source media object = VPU channel + 1
     * destination bind id = VENC channel + 7
     */
    bind[0] = vpu_chn + 1u;
    bind[1] = venc_chn + 7u;
    if ((rc = open_native()))
        return rc;
    rc = call_ioctl(media_fd, FH8626_MEDIA_BIND, bind);
    if (!rc)
        media_bound_mask |= 1u << vpu_chn;
    return rc;
}

int FH_SYS_UnBindbySrc(uint32_t source)
{
    int rc;

    if ((rc = open_native()))
        return rc;
    rc = call_ioctl(media_fd, FH8626_MEDIA_UNBIND_SRC, &source);
    if (!rc && source > 0u && source <= FH8626_VPU_CHANNELS)
        media_bound_mask &= ~(1u << (source - 1u));
    return rc;
}

int FH_SYS_UnBindbyDst(uint32_t destination)
{
    int rc;

    if ((rc = open_native()))
        return rc;
    return call_ioctl(media_fd, FH8626_MEDIA_UNBIND_DST, &destination);
}
int FH_SYS_BindVpu2Bgm(void) { return unsupported_feature("FH_SYS_BindVpu2Bgm"); }
int FH_SYS_BindVpu2Nn(void) { return unsupported_feature("FH_SYS_BindVpu2Nn"); }
int FH_SYS_Set_Resource(void *p) { (void)p; return unsupported_feature("FH_SYS_Set_Resource"); }
int FH_SYS_GetBindbyDest(void *p) { (void)p; return -ENOENT; }

/* --- VPSS / VPU --- */

int FH_VPSS_QuerySysMem(uint32_t *size)
{
    uint32_t q = 0;
    int rc;
    if (!size)
        return -EINVAL;
    if ((rc = open_native()))
        return rc;
    rc = call_ioctl(isp_fd, FH8626_VPU_MEM_QUERY, &q);
    if (!rc)
        *size = q;
    return rc;
}

int FH_VPSS_SysInitMem(void)
{
    uint32_t need = 0;
    int rc;

    if (vpu_sys.phys)
        return 0;
    rc = FH_VPSS_QuerySysMem(&need);
    if (rc)
        return rc;
    rc = alloc_vmm("majestic-vpu-sys", need, &vpu_sys);
    if (rc)
        return rc;
    return call_ioctl(isp_fd, FH8626_VPU_SYS_MEM_INIT, &vpu_sys);
}

int FH_VPSS_SetViAttr(void *attr)
{
    int rc;
    if (!attr)
        return -EINVAL;
    if ((rc = open_native()))
        return rc;
    trace_words("FH_VPSS_SetViAttr", 0, attr, 6);
    return call_ioctl(isp_fd, FH8626_VPU_SET_VI_ATTR, attr);
}

int FH_VPSS_GetViAttr(uint32_t out[2])
{
    uint32_t wire[3] = {0, 0, 0};
    int rc;

    if (!out)
        return -EINVAL;
    if ((rc = open_native()))
        return rc;
    rc = call_ioctl(isp_fd, FH8626_VPU_GET_VI_ATTR, wire);
    if (rc)
        return rc;

    /* FH8852 public wrapper exposes only the first two native VI words. */
    out[0] = wire[0];
    out[1] = wire[1];
    return 0;
}

int FH_VPSS_QueryChnMem(uint32_t chn, uint32_t width, uint32_t height,
                        uint32_t *size)
{
    struct vpu_query q = {chn, width, height, 0};
    int rc;

    if (!size || chn >= FH8626_VPU_CHANNELS)
        return -EINVAL;
    if ((rc = open_native()))
        return rc;
    rc = call_ioctl(isp_fd, FH8626_VPU_CHN_MEM_QUERY, &q);
    if (!rc)
        *size = q.size;
    return rc;
}

/* FH8852 donor uses (chn,width,height,allocator/cache selector). */
int FH_VPSS_ChnInitMem(uint32_t chn, uint32_t width, uint32_t height,
                       uint32_t mode)
{
    struct vpu_mem wire;
    uint32_t need = 0;
    int rc;
    (void)mode;

    if (chn >= FH8626_VPU_CHANNELS)
        return -ENOTSUP;
    if (vpu_chn[chn].phys)
        return 0;
    rc = FH_VPSS_QueryChnMem(chn, width, height, &need);
    if (rc)
        return rc;
    rc = alloc_vmm(chn ? "majestic-vpuN" : "majestic-vpu0",
                   need, &vpu_chn[chn]);
    if (rc)
        return rc;
    wire = (struct vpu_mem){chn, vpu_chn[chn].phys, vpu_chn[chn].virt,
                            vpu_chn[chn].size, width, height};
    return call_ioctl(isp_fd, FH8626_VPU_SET_CHN_MEM, &wire);
}

int FH_VPSS_SetChnAttr(uint32_t chn, const void *attr)
{
    const uint32_t *w = attr;
    struct channel_cfg cfg;
    int rc;

    if (!attr || chn >= FH8626_VPU_CHANNELS)
        return -EINVAL;

    /*
     * Recovered FH8852 wrapper: public attr is exactly {width,height};
     * the wrapper prepends channel and issues its 0x0c-byte SET_CHN_CFG.
     * FH8626 consumes the same three-word semantic record at 0xC00C6948.
     */
    trace_words("FH_VPSS_SetChnAttr", chn, attr, 2);
    if (w[0] < 32u || w[0] > 4096u ||
        w[1] < 32u || w[1] > 4096u)
        return -ERANGE;

    cfg = (struct channel_cfg){chn, w[0], w[1]};
    if ((rc = open_native()))
        return rc;
    return call_ioctl(isp_fd, FH8626_VPU_SET_CHN_CFG, &cfg);
}

int FH_VPSS_OpenChn(uint32_t chn)
{
    int rc;
    if ((rc = open_native()))
        return rc;
    return call_ioctl(isp_fd, FH8626_VPU_OPEN_CHN, &chn);
}

int FH_VPSS_CloseChn(uint32_t chn)
{
    int rc;

    if (chn > 4u)
        return -EINVAL;
    if ((rc = open_native()))
        return rc;
    return call_ioctl(isp_fd, FH8626_VPU_CLOSE_CHN, &chn);
}

int FH_VPSS_Enable(uint32_t chn)
{
    int rc;

    /*
     * Apollo FH_VPSS_Enable(channel) passes the channel word directly to
     * 0xC004694D. The payload is not a boolean enable flag.
     */
    if (chn > 4u)
        return -EINVAL;
    if ((rc = open_native()))
        return rc;
    return call_ioctl(isp_fd, FH8626_VPU_ENABLE, &chn);
}

int FH_VPSS_Disable(void)
{
    int rc;

    /*
     * Ghidra/isp.ko: 0xC004694E is the distinct global VPU disable request
     * and does not copy a userspace payload. Do not alias it to Enable(0).
     */
    if ((rc = open_native()))
        return rc;
    return call_ioctl(isp_fd, FH8626_VPU_DISABLE, NULL);
}

int FH_VPSS_SetFramectrl(uint32_t chn, const uint16_t pair[2])
{
    uint32_t wire[2];
    int rc;

    if (chn > 4u || !pair || !pair[0] || !pair[1])
        return -EINVAL;
    wire[0] = chn;
    wire[1] = (uint32_t)pair[0] | ((uint32_t)pair[1] << 16);
    if ((rc = open_native()))
        return rc;
    return call_ioctl(isp_fd, FH8626_VPU_SET_FRAMECTRL, wire);
}

int FH_VPSS_GetFramectrl(uint32_t chn, uint16_t pair[2])
{
    uint32_t wire[2] = {chn, 0};
    int rc;

    if (chn > 4u || !pair)
        return -EINVAL;
    if ((rc = open_native()))
        return rc;
    rc = call_ioctl(isp_fd, FH8626_VPU_GET_FRAMECTRL, wire);
    if (rc)
        return rc;
    pair[0] = (uint16_t)(wire[1] & 0xffffu);
    pair[1] = (uint16_t)(wire[1] >> 16);
    return 0;
}

int FH_VPSS_FreezeVideo(void) { return 0; }
int FH_VPSS_UnfreezeVideo(void) { return 0; }


int FH_VPSS_EnableYCmean(void)
{
    int rc;
    if ((rc = open_native()))
        return rc;
    return call_ioctl(isp_fd, FH8626_VPU_YCMEAN_OPEN, NULL);
}

int FH_VPSS_DisableYCmean(void)
{
    int rc;
    if ((rc = open_native()))
        return rc;
    return call_ioctl(isp_fd, FH8626_VPU_YCMEAN_CLOSE, NULL);
}

int FH_VPSS_SetYCmeanMode(uint32_t mode)
{
    int rc;
    if ((rc = open_native()))
        return rc;
    return call_ioctl(isp_fd, FH8626_VPU_SET_YCMEANMODE, &mode);
}

int FH_VPSS_GetYCmean(uint32_t out[5])
{
    uint32_t wire[7] = {0};
    int rc;

    if (!out)
        return -EINVAL;
    if ((rc = open_native()))
        return rc;
    rc = call_ioctl(isp_fd, FH8626_VPU_GET_MEAN, wire);
    if (rc)
        return rc;

    /*
     * Exact FH8852 public wrapper projection from the native 0x1c record:
     * pub[0] <- wire[6]
     * pub[1] <- wire[1]
     * pub[2] <- wire[2]
     * pub[3] <- wire[4]
     * pub[4] <- wire[5]
     */
    out[0] = wire[6];
    out[1] = wire[1];
    out[2] = wire[2];
    out[3] = wire[4];
    out[4] = wire[5];
    return 0;
}

int FH_VPSS_GetCPYData(uint32_t out[8])
{
    uint32_t wire[8] = {0};
    int rc;

    if (!out)
        return -EINVAL;
    if ((rc = open_native()))
        return rc;
    rc = call_ioctl(isp_fd, FH8626_VPU_GET_CPY, wire);
    if (rc)
        return rc;
    memcpy(out, wire, sizeof(wire));
    return 0;
}

int FH_VPSS_GetChnAttr(uint32_t chn, uint32_t out[2])
{
    uint32_t wire[3] = {chn, 0, 0};
    int rc;

    if (chn >= FH8626_VPU_CHANNELS || !out)
        return -EINVAL;
    if ((rc = open_native()))
        return rc;
    rc = call_ioctl(isp_fd, FH8626_VPU_GET_CHN_CFG, wire);
    if (rc)
        return rc;
    out[0] = wire[1];
    out[1] = wire[2];
    return 0;
}


static void graphv2_public_to_native(uint32_t selector,
                                     const uint32_t *pub, uint32_t *wire)
{
    unsigned i;

    /*
     * Recovered ABI:
     *
     * FH8852 public GraphV2 is 273 words:
     *   17-word header + two 128-word bitmap planes.
     *
     * The FH8852 driver receives this as one 0x48-byte header request
     * (0xC048564B/4D) followed by two 0x20C-byte plane transfers
     * (0xC20C564F/52).
     *
     * FH8626 folds the same data into one 0x448-byte request
     * (0xC448696D): one selector word + all 273 public words, with the
     * same header field permutation used by the FH8852 wrapper.
     */
    memset(wire, 0, FH8626_LOGOV2_WORDS * sizeof(*wire));
    wire[0] = selector;
    wire[1] = pub[1]; /* graph index */
    wire[2] = pub[0]; /* enable */
    for (i = 2; i <= 16; ++i)
        wire[i + 1] = pub[i];
    memcpy(&wire[18], &pub[17],
           2u * FH8626_LOGOV2_PLANE_WORDS * sizeof(uint32_t));
}

static void graphv2_native_to_public(const uint32_t *wire, uint32_t *pub)
{
    unsigned i;

    pub[0] = wire[2];
    pub[1] = wire[1];
    for (i = 2; i <= 16; ++i)
        pub[i] = wire[i + 1];
    memcpy(&pub[17], &wire[18],
           2u * FH8626_LOGOV2_PLANE_WORDS * sizeof(uint32_t));
}

static int graphv2_slot_valid(uint32_t selector, uint32_t index, int writable)
{
    if (selector > 2u)
        return 0;
    if (selector == 0u)
        return index < FH8626_LOGOV2_GLOBAL_SLOTS;
    return index < (writable ? FH8626_LOGOV2_CHN_SET_SLOTS
                             : FH8626_LOGOV2_CHN_GET_SLOTS);
}

static int graphv2_set(uint32_t selector, const uint32_t *pub)
{
    uint32_t wire[FH8626_LOGOV2_WORDS];
    int rc;

    if (!pub || !graphv2_slot_valid(selector, pub[1], 1))
        return -EINVAL;
    if ((rc = open_native()))
        return rc;

    graphv2_public_to_native(selector, pub, wire);
    return call_ioctl(isp_fd, FH8626_VPU_SET_LOGOV2, wire);
}

static int graphv2_get(uint32_t selector, uint32_t *pub)
{
    uint32_t wire[FH8626_LOGOV2_WORDS];
    uint32_t index;
    int rc;

    if (!pub)
        return -EINVAL;
    index = pub[1];
    if (!graphv2_slot_valid(selector, index, 0))
        return -EINVAL;
    if ((rc = open_native()))
        return rc;

    memset(wire, 0, sizeof(wire));
    wire[0] = selector;
    wire[1] = index;
    rc = call_ioctl(isp_fd, FH8626_VPU_GET_LOGOV2, wire);
    if (rc)
        return rc;

    memset(pub, 0, FH8852_GRAPHV2_WORDS * sizeof(*pub));
    graphv2_native_to_public(wire, pub);
    return 0;
}

_Static_assert(FH8626_LOGOV2_WORDS * sizeof(uint32_t) == 0x448,
               "FH8626 GraphV2 ioctl wire size");
_Static_assert(FH8852_GRAPHV2_WORDS == 17u + 2u * FH8626_LOGOV2_PLANE_WORDS,
               "FH8852 GraphV2 public shape");


int FH_VPSS_SetChnGraphV2(uint32_t chn, const uint32_t *graph)
{
    /*
     * FH8626 native logo number 0 is global; channel graphs are numbered
     * from 1. Stock product exposes two public video channels.
     */
    if (chn >= 2u)
        return -ENOTSUP;
    return graphv2_set(chn + 1u, graph);
}

int FH_VPSS_GetChnGraphV2(uint32_t chn, uint32_t *graph)
{
    if (chn >= 2u)
        return -ENOTSUP;
    return graphv2_get(chn + 1u, graph);
}

int FH_VPSS_SetGlbGraphV2(const uint32_t *graph)
{
    return graphv2_set(0u, graph);
}

int FH_VPSS_GetGlbGraphV2(uint32_t *graph)
{
    return graphv2_get(0u, graph);
}

/* --- VENC / PAE --- */

int FH_VENC_QuerySysMem(uint32_t *size)
{
    uint32_t q = 0;
    int rc;
    if (!size)
        return -EINVAL;
    if ((rc = open_native()))
        return rc;
    rc = call_ioctl(pae_fd, FH8626_PAE_SYS_QUERY, &q);
    if (!rc)
        *size = q;
    return rc;
}

int FH_VENC_SysInitMem(void)
{
    uint32_t need = 0;
    int rc;

    if (pae_sys.phys)
        return 0;
    rc = FH_VENC_QuerySysMem(&need);
    if (rc)
        return rc;
    rc = alloc_vmm("majestic-pae-sys", need, &pae_sys);
    if (rc)
        return rc;
    return call_ioctl(pae_fd, FH8626_PAE_SYS_INIT, &pae_sys);
}

int FH_VENC_QueryChnMem(uint32_t chn, uint32_t width, uint32_t height,
                        uint32_t refmode, uint32_t *size)
{
    struct pae_mem_query q = {chn, 0, width, height, refmode};
    int rc;
    if (!size)
        return -EINVAL;
    if ((rc = open_native()))
        return rc;
    rc = call_ioctl(pae_fd, FH8626_PAE_ENC_MEM_SIZE, &q);
    if (!rc)
        *size = q.size;
    return rc;
}

int FH_VENC_CreateChn(uint32_t chn, const void *attr)
{
    const uint32_t *a = attr;
    struct pae_mem_query q;
    uint32_t need = 0, candidate = 0;
    char name[24];
    int rc;

    if (chn >= FH8626_PRODUCT_VENC_CHANNELS)
        return -ENOTSUP;
    if (!attr)
        return -EINVAL;

    trace_words("FH_VENC_CreateChn", chn, attr, 3);
    if (!(a[0] & (4u | 8u)) || a[1] < 32u || a[2] < 32u)
        return -EINVAL;

    venc_support_type[chn] = a[0];
    venc_capacity_width[chn] = a[1];
    venc_capacity_height[chn] = a[2];

    if (!env_true("FH8626_MAJESTIC_NATIVE_VENC"))
        return strict_stub("FH_VENC_CreateChn");

    if ((rc = FH_VENC_SysInitMem()))
        return rc;
    if ((rc = open_native()))
        return rc;

    if (a[0] & 4u) {
        q = (struct pae_mem_query){chn, 0, a[1], a[2], 0};
        rc = call_ioctl(pae_fd, FH8626_PAE_ENC_MEM_SIZE, &q);
        if (rc)
            return rc;
        need = q.size;
    }
    if (a[0] & 8u) {
        q = (struct pae_mem_query){chn, 0, a[1], a[2], 1};
        rc = call_ioctl(pae_fd, FH8626_PAE_ENC_MEM_SIZE, &q);
        if (rc)
            return rc;
        candidate = q.size;
        if (candidate > need)
            need = candidate;
    }
    if (!need)
        return -EINVAL;

    if (!pae_chn[chn].phys) {
        snprintf(name, sizeof(name), "majestic-pae%u", chn);
        rc = alloc_vmm(name, need, &pae_chn[chn]);
        if (rc)
            return rc;
    } else if (pae_chn[chn].size < need) {
        return -ENOSPC;
    }
    return 0;
}

static int h264_translate_attr(uint32_t chn, const uint32_t *a,
                               struct pae_cfg *cfg, struct pae_rc *rate)
{
    uint32_t public_rc_mode;

    if (!a || !cfg || !rate)
        return -EINVAL;
    if (a[0] != 4u && a[0] != 8u)
        return -ENOTSUP;
    if (a[1] != 0x42u && a[1] != 0x4du)
        return -EINVAL;
    if (chn >= FH8626_PRODUCT_VENC_CHANNELS)
        return -ENOTSUP;
    if (a[3] < 32u || a[3] > venc_capacity_width[chn] ||
        a[4] < 32u || a[4] > venc_capacity_height[chn])
        return -ERANGE;

    memset(cfg, 0, sizeof(*cfg));
    memset(rate, 0, sizeof(*rate));

    /*
     * Canonical Apollo public H.264 attr -> PAE 0x2c wire:
     * public[0]  encode type (4 normal, 8 smart)
     * public[1]  H.264 profile (0x42 Baseline, 0x4d Main)
     * public[2]  GOP/key interval
     * public[3]  visible width
     * public[4]  visible height
     * public[5..8] smart-H.264 extras
     * public[21] RC mode selector
     *
     * The stock translator at Apollo 0x00211d9c constructs the same 11-word
     * PAE config and then immediately constructs the native 0x54 RC record.
     */
    cfg->chn = chn;
    cfg->width = a[3];
    cfg->height = a[4];
    cfg->field0c = a[2];
    cfg->profile = a[1];

    public_rc_mode = a[21];
    rate->chn = chn;

    switch (public_rc_mode) {
    case 3u: /* stock FH_RC_H264_VBR -> native mode 0 */
        rate->rc_mode = 0;
        rate->frame_rate_packed = a[28];
        rate->init_qp = a[22];
        rate->bitrate_or_rate = a[23];
        rate->i_min_qp = a[24];
        rate->i_max_qp = a[25];
        rate->p_min_qp = a[26];
        rate->p_max_qp = a[27];
        rate->i_proportion = a[32];
        rate->p_proportion = a[33];
        rate->fluctuate_level = a[34];
        rate->ip_qp_delta = (int32_t)a[31];
        rate->i_target_limit_bits = a[30];
        rate->max_rate_percent = a[29];
        break;
    case 4u: /* stock FH_RC_H264_CBR -> native mode 1 */
        rate->rc_mode = 1;
        rate->frame_rate_packed = a[24];
        rate->init_qp = a[22];
        rate->bitrate_or_rate = a[23];
        rate->i_min_qp = 10;
        rate->i_max_qp = 50;
        rate->p_min_qp = 10;
        rate->p_max_qp = 50;
        rate->i_proportion = a[28];
        rate->p_proportion = a[29];
        rate->fluctuate_level = a[30];
        rate->ip_qp_delta = (int32_t)a[27];
        rate->i_target_limit_bits = a[26];
        rate->max_rate_percent = a[25];
        break;
    case 5u: /* fixed-QP style public mode -> native mode 2 */
        rate->rc_mode = 2;
        rate->frame_rate_packed = a[24];
        rate->mode2_qp_a = a[22];
        rate->mode2_qp_b = a[23];
        rate->i_min_qp = a[22];
        rate->i_max_qp = a[22];
        rate->p_min_qp = a[23];
        rate->p_max_qp = a[23];
        break;
    case 6u: /* stock FH_RC_H264_AVBR -> native mode 4 */
        rate->rc_mode = 4;
        rate->frame_rate_packed = a[28];
        rate->init_qp = a[22];
        rate->bitrate_or_rate = a[23];
        rate->i_min_qp = a[24];
        rate->i_max_qp = a[25];
        rate->p_min_qp = a[26];
        rate->p_max_qp = a[27];
        rate->i_proportion = a[32];
        rate->p_proportion = a[33];
        rate->fluctuate_level = a[34];
        rate->ip_qp_delta = (int32_t)a[31];
        rate->i_target_limit_bits = a[30];
        rate->max_rate_percent = a[29];
        rate->still_rate_percent = a[35];
        rate->max_still_qp = a[36];
        break;
    case 0xbu: /* stock FH_RC_H264_CVBR -> native mode 5 */
        rate->rc_mode = 5;
        rate->frame_rate_packed = a[30];
        rate->init_qp = a[22];
        rate->bitrate_or_rate = a[24];
        rate->i_min_qp = a[26];
        rate->i_max_qp = a[27];
        rate->p_min_qp = a[28];
        rate->p_max_qp = a[29];
        rate->i_proportion = a[33];
        rate->p_proportion = a[34];
        rate->fluctuate_level = a[35];
        rate->ip_qp_delta = (int32_t)a[32];
        rate->i_target_limit_bits = a[31];
        rate->max_rate_percent = a[25];
        rate->still_rate_percent = 30;
        rate->max_still_qp = 34;
        rate->additional_rate_bits = a[23];
        rate->extra_qp_parameter = a[36];
        break;
    default:
        return -ENOTSUP;
    }

    if (!(rate->frame_rate_packed & 0xffffu) ||
        !(rate->frame_rate_packed >> 16) ||
        rate->init_qp > 51u)
        return -EINVAL;

    if (a[0] == 8u) {
        cfg->field20 = a[5];
        cfg->field24 = a[6];
        cfg->field28 = a[7];
        cfg->mode = a[8];
    }
    return 0;
}

static int native_venc_from_public_attr(uint32_t chn, const uint32_t *attr)
{
    struct pae_mem mem;
    struct pae_cfg cfg;
    struct pae_rc rate;
    uint32_t refmode;
    int rc;

    if (chn >= FH8626_PRODUCT_VENC_CHANNELS)
        return -ENOTSUP;
    if ((rc = h264_translate_attr(chn, attr, &cfg, &rate)))
        return rc;
    if ((rc = open_native()))
        return rc;
    if ((rc = FH_VENC_SysInitMem()))
        return rc;
    if (!pae_chn[chn].phys)
        return -EPIPE;

    refmode = attr[0] == 8u ? 1u : 0u;
    if (!(venc_support_type[chn] & attr[0]))
        return -ENOTSUP;

    /*
     * Stock CreateChn allocates for channel capacity; SetChnAttr selects
     * normal/smart H.264 and initializes that allocation with the capacity
     * geometry before programming visible encoder geometry.
     */
    mem = (struct pae_mem){chn, pae_chn[chn].phys, pae_chn[chn].virt,
                           pae_chn[chn].size, venc_capacity_width[chn],
                           venc_capacity_height[chn], refmode};
    rc = call_ioctl(pae_fd, FH8626_PAE_ENC_MEM_INIT, &mem);
    if (rc)
        return rc;

    rc = call_ioctl(pae_fd, FH8626_PAE_SET_CONFIG, &cfg);
    if (!rc)
        rc = call_ioctl(pae_fd, FH8626_PAE_SET_RC, &rate);
    if (!rc)
        pae_configured[chn] = 1;
    return rc;
}

int FH_VENC_SetChnAttr(uint32_t chn, const void *attr)
{
    if (!attr)
        return -EINVAL;
    trace_words("FH_VENC_SetChnAttr", chn, attr, 37);
    if (env_true("FH8626_MAJESTIC_NATIVE_VENC"))
        return native_venc_from_public_attr(chn, (const uint32_t *)attr);
    return strict_stub("FH_VENC_SetChnAttr");
}

int FH_VENC_StartRecvPic(uint32_t chn)
{
    int rc;

    if (chn >= FH8626_PRODUCT_VENC_CHANNELS)
        return -ENOTSUP;
    if (!env_true("FH8626_MAJESTIC_NATIVE_VENC"))
        return strict_stub("FH_VENC_StartRecvPic");
    if (!pae_configured[chn])
        return -EPIPE;
    if ((rc = open_native()))
        return rc;

    /*
     * Stock Apollo owns VI/VPU enable and ISP producer gating outside the
     * per-channel VENC start. Do not toggle the global producer gate here:
     * doing so makes stopping a substream capable of killing the main stream.
     */
    return call_ioctl(pae_fd, FH8626_PAE_START_RECV, &chn);
}

int FH_VENC_StopRecvPic(uint32_t chn)
{
    int rc, release_rc = 0;

    if (chn >= FH8626_PRODUCT_VENC_CHANNELS)
        return -ENOTSUP;
    if ((rc = open_native()))
        return rc;

    if (stream_lease_held && stream_lease_channel == chn) {
        release_rc = call_ioctl(pae_fd, FH8626_PAE_STREAM_STEP, &chn);
        if (!release_rc)
            stream_lease_held = 0;
    }
    rc = call_ioctl(pae_fd, FH8626_PAE_STOP_RECV, &chn);
    return release_rc ? release_rc : rc;
}

int FH_VENC_RequestIDR(uint32_t chn)
{
    int rc;
    if (chn >= FH8626_PRODUCT_VENC_CHANNELS)
        return -ENOTSUP;
    if ((rc = open_native()))
        return rc;
    return call_ioctl(pae_fd, FH8626_PAE_FORCE_I, &chn);
}

static int fill_public_stream(void *stream)
{
    uint32_t *out = stream;
    uint32_t virt, len, ts, tail, packs;

    if (!stream || !pae_sys.virt || !pae_sys.size)
        return -EINVAL;
    if (stream_desc[1] != FH8626_MEDIA_STREAM_KIND)
        return -EAGAIN;
    if (stream_desc[3] != stream_lease_channel)
        return -EAGAIN;

    virt = stream_desc[7];
    len = stream_desc[8];
    ts = stream_desc[10];
    if (!len || len > pae_sys.size || virt < pae_sys.virt ||
        virt >= pae_sys.virt + pae_sys.size)
        return -ERANGE;

    memset(stream, 0, FH8852_PUBLIC_STREAM_SIZE);

    /*
     * FH8852 fh_venc_handle_stream() produces this public layout:
     *   +0x00 kind
     *   +0x1c pack count
     *   +0x20 pack[0], 12 bytes each
     *   +0x130/+0x134 64-bit converted timestamp
     *
     * A pack is copied by the donor as {raw+0x30, raw+0x3c, raw+0x38}.
     * For the FH8626 ring bridge we expose {virtual address, bytes, 0}.
     * Two packs are used only when the encoded AU wraps around the ring.
     */
    out[0x00 / 4] = FH8626_MEDIA_STREAM_KIND;
    out[0x08 / 4] = virt;
    out[0x10 / 4] = len;
    out[0x14 / 4] = ts;

    tail = pae_sys.virt + pae_sys.size - virt;
    packs = len <= tail ? 1u : 2u;
    out[0x1c / 4] = packs;

    out[0x20 / 4] = virt;
    out[0x24 / 4] = len <= tail ? len : tail;
    out[0x28 / 4] = 0;

    if (packs == 2u) {
        out[0x2c / 4] = pae_sys.virt;
        out[0x30 / 4] = len - tail;
        out[0x34 / 4] = 0;
    }

    out[0x130 / 4] = ts;
    out[0x134 / 4] = 0;
    return 0;
}

static int acquire_stream(uint32_t chn, void *stream)
{
    int rc;

    if (chn >= FH8626_PRODUCT_VENC_CHANNELS || !stream)
        return -EINVAL;
    if (stream_lease_held)
        return -EBUSY;
    if ((rc = open_native()))
        return rc;

    memset(stream_desc, 0, sizeof(stream_desc));
    stream_desc[0] = FH8626_MEDIA_STREAM_KIND;
    rc = call_ioctl(media_fd, FH8626_MEDIA_STREAM, stream_desc);
    if (rc)
        return rc;

    /*
     * media_process.ko wraps the encoder's 0x168 record:
     * desc[1] = inner kind (4 for H.264)
     * desc[2] = inner kind again (start of copied inner record)
     * desc[3] = encoder channel
     * Query is a FIFO peek; release performs the pop. If the head belongs to
     * another channel, leave it owned by that channel's consumer.
     */
    if (stream_desc[1] != FH8626_MEDIA_STREAM_KIND ||
        stream_desc[3] != chn)
        return -EAGAIN;

    stream_lease_channel = chn;
    rc = fill_public_stream(stream);
    if (rc) {
        (void)call_ioctl(pae_fd, FH8626_PAE_STREAM_STEP, &chn);
        return rc;
    }
    stream_lease_held = 1;
    return 0;
}

int FH_VENC_GetStream(uint32_t chn, void *stream)
{
    return acquire_stream(chn, stream);
}

int FH_VENC_GetStream_Block(uint32_t chn, void *stream)
{
    const char *wait_env = getenv("FH8626_MAJESTIC_STREAM_WAIT_MS");
    unsigned long wait_ms = wait_env && wait_env[0] ?
        strtoul(wait_env, NULL, 0) : 200ul;
    unsigned long elapsed = 0;
    int rc;

    /*
     * FH8626 has no binary-compatible equivalent of the FH8852 blocking
     * 0xC1A04D06 request. Emulate its bounded wait in userspace by polling the
     * recovered dequeue contract. This avoids an unbounded thread hang during
     * bring-up while preserving a useful blocking surface for Majestic.
     */
    do {
        rc = acquire_stream(chn, stream);
        if (rc != -EAGAIN && rc != -EIO && rc != -ETIMEDOUT)
            return rc;
        if (elapsed >= wait_ms)
            return -EAGAIN;
        usleep(1000);
        elapsed++;
    } while (1);
}

int FH_VENC_ReleaseStream(uint32_t chn, void *stream)
{
    int rc;

    (void)stream;
    if (chn >= FH8626_PRODUCT_VENC_CHANNELS)
        return -EINVAL;
    if (!stream_lease_held || stream_lease_channel != chn)
        return -EPERM;
    if ((rc = open_native()))
        return rc;
    rc = call_ioctl(pae_fd, FH8626_PAE_STREAM_STEP, &chn);
    if (!rc)
        stream_lease_held = 0;
    return rc;
}

static int h264_translate_public_rc(uint32_t chn, const uint32_t *a,
                                    struct pae_rc *rate)
{
    if (!a || !rate)
        return -EINVAL;
    memset(rate, 0, sizeof(*rate));
    rate->chn = chn;

    switch (a[0]) {
    case 3u: /* FH_RC_H264_VBR */
        rate->rc_mode = 0;
        rate->frame_rate_packed = a[7];
        rate->init_qp = a[1];
        rate->bitrate_or_rate = a[2];
        rate->i_min_qp = a[3];
        rate->i_max_qp = a[4];
        rate->p_min_qp = a[5];
        rate->p_max_qp = a[6];
        rate->i_proportion = a[11];
        rate->p_proportion = a[12];
        rate->fluctuate_level = a[13];
        rate->ip_qp_delta = (int32_t)a[10];
        rate->i_target_limit_bits = a[9];
        rate->max_rate_percent = a[8];
        break;
    case 4u: /* FH_RC_H264_CBR */
        rate->rc_mode = 1;
        rate->frame_rate_packed = a[3];
        rate->init_qp = a[1];
        rate->bitrate_or_rate = a[2];
        rate->i_min_qp = 10;
        rate->i_max_qp = 50;
        rate->p_min_qp = 10;
        rate->p_max_qp = 50;
        rate->i_proportion = a[7];
        rate->p_proportion = a[8];
        rate->fluctuate_level = a[9];
        rate->ip_qp_delta = (int32_t)a[6];
        rate->i_target_limit_bits = a[5];
        rate->max_rate_percent = a[4];
        break;
    case 5u: /* fixed-QP public mode */
        rate->rc_mode = 2;
        rate->frame_rate_packed = a[3];
        rate->mode2_qp_a = a[1];
        rate->mode2_qp_b = a[2];
        rate->i_min_qp = a[1];
        rate->i_max_qp = a[1];
        rate->p_min_qp = a[2];
        rate->p_max_qp = a[2];
        break;
    case 6u: /* FH_RC_H264_AVBR */
        rate->rc_mode = 4;
        rate->frame_rate_packed = a[7];
        rate->init_qp = a[1];
        rate->bitrate_or_rate = a[2];
        rate->i_min_qp = a[3];
        rate->i_max_qp = a[4];
        rate->p_min_qp = a[5];
        rate->p_max_qp = a[6];
        rate->i_proportion = a[11];
        rate->p_proportion = a[12];
        rate->fluctuate_level = a[13];
        rate->ip_qp_delta = (int32_t)a[10];
        rate->i_target_limit_bits = a[9];
        rate->max_rate_percent = a[8];
        rate->still_rate_percent = a[14];
        rate->max_still_qp = a[15];
        break;
    case 0xbu: /* FH_RC_H264_CVBR */
        rate->rc_mode = 5;
        rate->frame_rate_packed = a[9];
        rate->init_qp = a[1];
        rate->bitrate_or_rate = a[3];
        rate->i_min_qp = a[5];
        rate->i_max_qp = a[6];
        rate->p_min_qp = a[7];
        rate->p_max_qp = a[8];
        rate->i_proportion = a[12];
        rate->p_proportion = a[13];
        rate->fluctuate_level = a[14];
        rate->ip_qp_delta = (int32_t)a[11];
        rate->i_target_limit_bits = a[10];
        rate->max_rate_percent = a[4];
        rate->still_rate_percent = 30;
        rate->max_still_qp = 34;
        rate->additional_rate_bits = a[2];
        rate->extra_qp_parameter = a[15];
        break;
    default:
        return -ENOTSUP;
    }

    if (!(rate->frame_rate_packed & 0xffffu) ||
        !(rate->frame_rate_packed >> 16) ||
        rate->init_qp > 51u)
        return -EINVAL;
    return 0;
}
static int h264_native_rc_to_public(const struct pae_rc *rate,
                                    uint32_t *a, size_t words)
{
    if (!rate || !a || words < 16u)
        return -EINVAL;
    memset(a, 0, words * sizeof(*a));

    switch (rate->rc_mode) {
    case 0u:
        a[0] = 3u;
        a[1] = rate->init_qp;
        a[2] = rate->bitrate_or_rate;
        a[3] = rate->i_min_qp;
        a[4] = rate->i_max_qp;
        a[5] = rate->p_min_qp;
        a[6] = rate->p_max_qp;
        a[7] = rate->frame_rate_packed;
        a[8] = rate->max_rate_percent;
        a[9] = rate->i_target_limit_bits;
        a[10] = (uint32_t)rate->ip_qp_delta;
        a[11] = rate->i_proportion;
        a[12] = rate->p_proportion;
        a[13] = rate->fluctuate_level;
        return 0;
    case 1u:
        a[0] = 4u;
        a[1] = rate->init_qp;
        a[2] = rate->bitrate_or_rate;
        a[3] = rate->frame_rate_packed;
        a[4] = rate->max_rate_percent;
        a[5] = rate->i_target_limit_bits;
        a[6] = (uint32_t)rate->ip_qp_delta;
        a[7] = rate->i_proportion;
        a[8] = rate->p_proportion;
        a[9] = rate->fluctuate_level;
        return 0;
    case 2u:
        a[0] = 5u;
        a[1] = rate->mode2_qp_a;
        a[2] = rate->mode2_qp_b;
        a[3] = rate->frame_rate_packed;
        return 0;
    case 4u:
        a[0] = 6u;
        a[1] = rate->init_qp;
        a[2] = rate->bitrate_or_rate;
        a[3] = rate->i_min_qp;
        a[4] = rate->i_max_qp;
        a[5] = rate->p_min_qp;
        a[6] = rate->p_max_qp;
        a[7] = rate->frame_rate_packed;
        a[8] = rate->max_rate_percent;
        a[9] = rate->i_target_limit_bits;
        a[10] = (uint32_t)rate->ip_qp_delta;
        a[11] = rate->i_proportion;
        a[12] = rate->p_proportion;
        a[13] = rate->fluctuate_level;
        a[14] = rate->still_rate_percent;
        a[15] = rate->max_still_qp;
        return 0;
    case 5u:
        a[0] = 0xbu;
        a[1] = rate->init_qp;
        a[2] = rate->additional_rate_bits;
        a[3] = rate->bitrate_or_rate;
        a[4] = rate->max_rate_percent;
        a[5] = rate->i_min_qp;
        a[6] = rate->i_max_qp;
        a[7] = rate->p_min_qp;
        a[8] = rate->p_max_qp;
        a[9] = rate->frame_rate_packed;
        a[10] = rate->i_target_limit_bits;
        a[11] = (uint32_t)rate->ip_qp_delta;
        a[12] = rate->i_proportion;
        a[13] = rate->p_proportion;
        a[14] = rate->fluctuate_level;
        a[15] = rate->extra_qp_parameter;
        return 0;
    default:
        return -ENOTSUP;
    }
}


int FH_VENC_SetRCAttr(uint32_t chn, const void *attr)
{
    struct pae_rc rate;
    int rc;

    if (!attr || chn >= FH8626_PRODUCT_VENC_CHANNELS)
        return -EINVAL;
    trace_words("FH_VENC_SetRCAttr", chn, attr, 16);
    if (!env_true("FH8626_MAJESTIC_NATIVE_VENC"))
        return strict_stub("FH_VENC_SetRCAttr");
    if ((rc = h264_translate_public_rc(chn, attr, &rate)))
        return rc;
    if ((rc = open_native()))
        return rc;
    return call_ioctl(pae_fd, FH8626_PAE_SET_RC, &rate);
}

int FH_VENC_GetChnAttr(uint32_t chn, void *attr)
{
    struct pae_cfg cfg;
    struct pae_rc rate;
    uint32_t public_rc[16];
    uint32_t *a = attr;
    int rc;

    if (!attr || chn >= FH8626_PRODUCT_VENC_CHANNELS)
        return -EINVAL;
    if (!env_true("FH8626_MAJESTIC_NATIVE_VENC"))
        return strict_stub("FH_VENC_GetChnAttr");
    if ((rc = open_native()))
        return rc;

    memset(&cfg, 0, sizeof(cfg));
    cfg.chn = chn;
    rc = call_ioctl(pae_fd, FH8626_PAE_GET_CONFIG, &cfg);
    if (rc)
        return rc;

    memset(&rate, 0, sizeof(rate));
    rate.chn = chn;
    rc = call_ioctl(pae_fd, FH8626_PAE_GET_RC, &rate);
    if (rc)
        return rc;
    rc = h264_native_rc_to_public(&rate, public_rc, 16u);
    if (rc)
        return rc;

    memset(a, 0, 37u * sizeof(*a));
    a[0] = (cfg.field20 || cfg.field24 || cfg.field28 || cfg.mode) ? 8u : 4u;
    a[1] = cfg.profile;
    a[2] = cfg.field0c;
    a[3] = cfg.width;
    a[4] = cfg.height;
    if (a[0] == 8u) {
        a[5] = cfg.field20;
        a[6] = cfg.field24;
        a[7] = cfg.field28;
        a[8] = cfg.mode;
    }

    a[21] = public_rc[0];
    switch (public_rc[0]) {
    case 3u:
        memcpy(&a[22], &public_rc[1], 13u * sizeof(uint32_t));
        break;
    case 4u:
        memcpy(&a[22], &public_rc[1], 9u * sizeof(uint32_t));
        break;
    case 5u:
        memcpy(&a[22], &public_rc[1], 3u * sizeof(uint32_t));
        break;
    case 6u:
    case 0xbu:
        memcpy(&a[22], &public_rc[1], 15u * sizeof(uint32_t));
        break;
    default:
        return -ENOTSUP;
    }
    return 0;
}

int FH_VENC_SetRcChangeParam(uint32_t chn, const void *attr)
{
    uint32_t wire[7];
    int rc;

    if (!attr || chn >= FH8626_PRODUCT_VENC_CHANNELS)
        return -EINVAL;
    trace_words("FH_VENC_SetRcChangeParam", chn, attr, 6);
    if (!env_true("FH8626_MAJESTIC_NATIVE_VENC"))
        return strict_stub("FH_VENC_SetRcChangeParam");

    /*
     * Apollo realtime RC backend copies six public words unchanged after the
     * channel id and issues the recovered 0x1c-byte request 0xC01C5055.
     */
    wire[0] = chn;
    memcpy(&wire[1], attr, 6u * sizeof(uint32_t));
    if ((rc = open_native()))
        return rc;
    return call_ioctl(pae_fd, 0xC01C5055UL, wire);
}

int FH_VENC_GetRCAttr(uint32_t chn, void *attr)
{
    struct pae_rc rate;
    int rc;

    if (!attr || chn >= FH8626_PRODUCT_VENC_CHANNELS)
        return -EINVAL;
    if (!env_true("FH8626_MAJESTIC_NATIVE_VENC"))
        return strict_stub("FH_VENC_GetRCAttr");
    if ((rc = open_native()))
        return rc;

    memset(&rate, 0, sizeof(rate));
    rate.chn = chn;
    rc = call_ioctl(pae_fd, FH8626_PAE_GET_RC, &rate);
    if (rc)
        return rc;
    return h264_native_rc_to_public(&rate, attr, 16u);
}

/* Loader/dlsym compatibility for common optional controls. */
#define SIMPLE_STUB0(name) int name(void) { return unsupported_feature(#name); }
SIMPLE_STUB0(FH_VENC_ClearRoi)
SIMPLE_STUB0(FH_VENC_ClearYuvQueue)
SIMPLE_STUB0(FH_VENC_SetRotate)
SIMPLE_STUB0(FH_VENC_GetRotate)
SIMPLE_STUB0(FH_VENC_SetRoiCfg)
SIMPLE_STUB0(FH_VENC_SetBkgQP)
SIMPLE_STUB0(FH_VENC_GetBkgQP)
SIMPLE_STUB0(FH_VENC_SetEncParam)
SIMPLE_STUB0(FH_VENC_GetEncParam)
SIMPLE_STUB0(FH_VENC_SetLostFrameAttr)
SIMPLE_STUB0(FH_VENC_GetLostFrameAttr)
SIMPLE_STUB0(FH_VPSS_SetCrop)
SIMPLE_STUB0(FH_VPSS_GetCrop)
SIMPLE_STUB0(FH_VPSS_SetRotate)
SIMPLE_STUB0(FH_VPSS_Reset)
SIMPLE_STUB0(FH_VPSS_SetLDCAttr)
SIMPLE_STUB0(FH_VPSS_GetLDCAttr)
SIMPLE_STUB0(FH_VPSS_SetVOMode)
SIMPLE_STUB0(FH_VPSS_SetVORotate)
SIMPLE_STUB0(FH_VPSS_SetScalerCoeff)
SIMPLE_STUB0(FH_VPSS_SetChnViSel)
SIMPLE_STUB0(FH_VENC_GetChnStatus)
SIMPLE_STUB0(FH_VENC_GetCurPts)
SIMPLE_STUB0(FH_VENC_SetH264Entropy)
SIMPLE_STUB0(FH_VENC_GetH264Entropy)
SIMPLE_STUB0(FH_VENC_SetH264Dblk)
SIMPLE_STUB0(FH_VENC_GetH264Dblk)
SIMPLE_STUB0(FH_VENC_SetH264SliceSplit)
SIMPLE_STUB0(FH_VENC_GetH264SliceSplit)
SIMPLE_STUB0(FH_VENC_SetH264IntraFresh)
SIMPLE_STUB0(FH_VENC_GetH264IntraFresh)
SIMPLE_STUB0(FH_VENC_SetEncRefMode)
SIMPLE_STUB0(FH_VENC_GetEncRefMode)

/* --- JPEG / MJPEG --- */

int _JPEG_SysInit(void)
{
    memset(jpeg_mode, 0, sizeof(jpeg_mode));
    memset(jpeg_mem_state, 0, sizeof(jpeg_mem_state));
    memset(jpeg_attr_valid, 0, sizeof(jpeg_attr_valid));
    memset(jpeg_rc_valid, 0, sizeof(jpeg_rc_valid));
    jpeg_snapshot_channel = UINT32_MAX;
    jpeg_mjpeg_channel = UINT32_MAX;
    return open_jpeg();
}

int _JPEG_QueryChnMem(void *opaque, uint32_t chn, uint32_t width,
                      uint32_t height, uint32_t is_mjpeg, uint32_t *size)
{
    struct jpeg_query q = {
        is_mjpeg ? FH8626_JPEG_MODE_MJPEG : FH8626_JPEG_MODE_SNAPSHOT,
        width, height, 0
    };
    int rc;
    (void)opaque;
    (void)chn;

    if (!size || !width || !height)
        return -EINVAL;
    if ((rc = open_jpeg()))
        return rc;
    rc = call_ioctl(jpeg_fd, FH8626_JPEG_MEM_QUERY, &q);
    if (!rc)
        *size = q.size;
    return rc;
}

int _JPEG_CreateChn(void *opaque, uint32_t chn, uint32_t width,
                    uint32_t height, uint32_t is_mjpeg,
                    uint32_t phys, uint32_t virt, uint32_t size)
{
    struct jpeg_mem m;
    uint32_t mode;
    int rc;
    (void)opaque;

    if (chn >= FH8626_JPEG_CHANNELS || !width || !height ||
        !phys || !virt || !size)
        return -EINVAL;

    mode = is_mjpeg ? FH8626_JPEG_MODE_MJPEG : FH8626_JPEG_MODE_SNAPSHOT;
    m = (struct jpeg_mem){mode, phys, virt, size, width, height};
    if ((rc = open_jpeg()))
        return rc;
    rc = call_ioctl(jpeg_fd, FH8626_JPEG_MEM_INIT, &m);
    if (rc)
        return rc;

    jpeg_mode[chn] = mode;
    jpeg_mem_state[chn] = (struct mem3){phys, virt, size};
    jpeg_width[chn] = width;
    jpeg_height[chn] = height;
    if (mode == FH8626_JPEG_MODE_SNAPSHOT)
        jpeg_snapshot_channel = chn;
    else
        jpeg_mjpeg_channel = chn;
    return 0;
}

int _JPEG_DestroyChn(void *opaque, uint32_t chn)
{
    struct jpeg_mem m;
    uint32_t mode;
    int rc;
    (void)opaque;

    if (chn >= FH8626_JPEG_CHANNELS || !(mode = jpeg_mode[chn]))
        return -EINVAL;
    if ((rc = open_jpeg()))
        return rc;

    m = (struct jpeg_mem){mode,
        jpeg_mem_state[chn].phys, jpeg_mem_state[chn].virt,
        jpeg_mem_state[chn].size, 0, 0};
    rc = call_ioctl(jpeg_fd, FH8626_JPEG_MEM_UNINIT, &m);
    if (rc)
        return rc;

    if (jpeg_snapshot_channel == chn)
        jpeg_snapshot_channel = UINT32_MAX;
    if (jpeg_mjpeg_channel == chn)
        jpeg_mjpeg_channel = UINT32_MAX;
    jpeg_mode[chn] = 0;
    memset(&jpeg_mem_state[chn], 0, sizeof(jpeg_mem_state[chn]));
    jpeg_attr_valid[chn] = 0;
    jpeg_rc_valid[chn] = 0;
    return 0;
}

static int jpeg_public_attr_to_wire(uint32_t mode, const uint32_t *a,
                                    void *wire)
{
    if (!a || !wire)
        return -EINVAL;

    if (mode == FH8626_JPEG_MODE_SNAPSHOT) {
        struct jpeg_snapshot_cfg *cfg = wire;
        if (a[0] != FH8626_JPEG_MODE_SNAPSHOT || a[2] > 3u || a[3] > 9u)
            return -EINVAL;
        cfg->speed_or_mode = a[1];
        cfg->mode = FH8626_JPEG_MODE_SNAPSHOT;
        cfg->quality_hw = jpeg_quality_lut[a[3]];
        cfg->rotate = a[2];
        return 0;
    }

    if (mode == FH8626_JPEG_MODE_MJPEG) {
        struct jpeg_mjpeg_cfg *cfg = wire;
        uint32_t rc_mode;

        if (a[0] != FH8626_JPEG_MODE_MJPEG || a[3] > 3u || a[4] > 9u)
            return -EINVAL;
        memset(cfg, 0, sizeof(*cfg));
        cfg->word[0] = FH8626_JPEG_MODE_MJPEG;
        cfg->word[1] = a[1];
        cfg->word[2] = a[2];
        cfg->word[10] = 1;
        cfg->word[11] = jpeg_quality_lut[a[4]];
        cfg->word[12] = a[3];

        rc_mode = a[21];
        switch (rc_mode) {
        case 0:
            cfg->word[3] = a[23] & 0xffffu;
            cfg->word[4] = a[23] >> 16;
            cfg->word[5] = 0;
            cfg->word[6] = a[22];
            cfg->word[7] = 0;
            cfg->word[8] = a[22];
            cfg->word[9] = a[22];
            break;
        case 1:
            cfg->word[3] = a[24] & 0xffffu;
            cfg->word[4] = a[24] >> 16;
            cfg->word[5] = 1;
            cfg->word[6] = a[22];
            cfg->word[7] = a[23];
            cfg->word[8] = 0;
            cfg->word[9] = 98;
            break;
        case 2:
            cfg->word[3] = a[26] & 0xffffu;
            cfg->word[4] = a[26] >> 16;
            cfg->word[5] = 1;
            cfg->word[6] = a[22];
            cfg->word[7] = a[23];
            cfg->word[8] = a[24];
            cfg->word[9] = a[25];
            break;
        default:
            return -ENOTSUP;
        }
        if (!cfg->word[3] || !cfg->word[4])
            return -EINVAL;
        return 0;
    }

    return -ENOTSUP;
}

int _JPEG_SetChnAttr(void *opaque, uint32_t chn, const uint32_t *attr)
{
    union {
        struct jpeg_snapshot_cfg jpg;
        struct jpeg_mjpeg_cfg mjpg;
    } wire;
    uint32_t mode;
    int rc;
    (void)opaque;

    if (chn >= FH8626_JPEG_CHANNELS || !attr || !(mode = jpeg_mode[chn]))
        return -EINVAL;
    memset(&wire, 0, sizeof(wire));
    if ((rc = jpeg_public_attr_to_wire(mode, attr, &wire)))
        return rc;
    if ((rc = open_jpeg()))
        return rc;

    rc = call_ioctl(jpeg_fd,
        mode == FH8626_JPEG_MODE_SNAPSHOT ?
            FH8626_JPEG_SET_CHN_CFG : FH8626_MJPEG_SET_CHN_CFG,
        &wire);
    if (rc)
        return rc;
    memcpy(jpeg_attr[chn], attr, sizeof(jpeg_attr[chn]));
    jpeg_attr_valid[chn] = 1;
    return 0;
}

int _JPEG_GetChnAttr(void *opaque, uint32_t chn, uint32_t *attr)
{
    uint32_t mode;
    int rc;
    (void)opaque;

    if (chn >= FH8626_JPEG_CHANNELS || !attr || !(mode = jpeg_mode[chn]))
        return -EINVAL;
    if (!jpeg_attr_valid[chn])
        return -EAGAIN;
    if ((rc = open_jpeg()))
        return rc;

    memcpy(attr, jpeg_attr[chn], sizeof(jpeg_attr[chn]));
    if (mode == FH8626_JPEG_MODE_SNAPSHOT) {
        struct jpeg_snapshot_cfg cfg = {0};
        rc = call_ioctl(jpeg_fd, FH8626_JPEG_GET_CHN_CFG, &cfg);
        if (rc)
            return rc;
        attr[0] = mode;
        attr[1] = cfg.speed_or_mode;
        attr[2] = cfg.rotate;
    } else {
        struct jpeg_mjpeg_cfg cfg;
        memset(&cfg, 0, sizeof(cfg));
        rc = call_ioctl(jpeg_fd, FH8626_MJPEG_GET_CHN_CFG, &cfg);
        if (rc)
            return rc;
        attr[0] = mode;
        attr[1] = cfg.word[1];
        attr[2] = cfg.word[2];
        attr[3] = cfg.word[12];
    }
    return 0;
}

static int jpeg_public_rc_to_wire(const uint32_t *a, struct jpeg_rc *r)
{
    if (!a || !r)
        return -EINVAL;
    memset(r, 0, sizeof(*r));

    switch (a[0]) {
    case 0:
        r->word[0] = a[2] & 0xffffu;
        r->word[1] = a[2] >> 16;
        r->word[2] = 0;
        r->word[3] = a[1];
        r->word[4] = 0;
        r->word[5] = a[1];
        r->word[6] = a[1];
        break;
    case 1:
        r->word[0] = a[3] & 0xffffu;
        r->word[1] = a[3] >> 16;
        r->word[2] = 1;
        r->word[3] = a[1];
        r->word[4] = a[2];
        r->word[5] = 0;
        r->word[6] = 98;
        break;
    case 2:
        r->word[0] = a[5] & 0xffffu;
        r->word[1] = a[5] >> 16;
        r->word[2] = 1;
        r->word[3] = a[1];
        r->word[4] = a[2];
        r->word[5] = a[3];
        r->word[6] = a[4];
        break;
    default:
        return -ENOTSUP;
    }
    if (!r->word[0] || !r->word[1])
        return -EINVAL;
    return 0;
}

static int jpeg_wire_rc_to_public(const struct jpeg_rc *r, uint32_t mode,
                                  uint32_t *a)
{
    if (!r || !a || mode > 2u)
        return -EINVAL;
    memset(a, 0, 17u * sizeof(*a));
    a[0] = mode;
    switch (mode) {
    case 0:
        a[1] = r->word[3];
        a[2] = (r->word[0] & 0xffffu) | (r->word[1] << 16);
        break;
    case 1:
        a[1] = r->word[3];
        a[2] = r->word[4];
        a[3] = (r->word[0] & 0xffffu) | (r->word[1] << 16);
        break;
    case 2:
        a[1] = r->word[3];
        a[2] = r->word[4];
        a[3] = r->word[5];
        a[4] = r->word[6];
        a[5] = (r->word[0] & 0xffffu) | (r->word[1] << 16);
        break;
    }
    return 0;
}

int _JPEG_SetRCAttr(void *opaque, uint32_t chn, const uint32_t *attr)
{
    struct jpeg_rc wire;
    int rc;
    (void)opaque;

    if (chn >= FH8626_JPEG_CHANNELS || !attr ||
        jpeg_mode[chn] != FH8626_JPEG_MODE_MJPEG)
        return -EINVAL;
    if ((rc = jpeg_public_rc_to_wire(attr, &wire)))
        return rc;
    if ((rc = open_jpeg()))
        return rc;
    rc = call_ioctl(jpeg_fd, FH8626_MJPEG_SET_RC, &wire);
    if (rc)
        return rc;
    memcpy(jpeg_rc_attr[chn], attr, sizeof(jpeg_rc_attr[chn]));
    jpeg_rc_valid[chn] = 1;
    return 0;
}

int _JPEG_GetRCAttr(void *opaque, uint32_t chn, uint32_t *attr)
{
    struct jpeg_rc wire;
    uint32_t mode;
    int rc;
    (void)opaque;

    if (chn >= FH8626_JPEG_CHANNELS || !attr ||
        jpeg_mode[chn] != FH8626_JPEG_MODE_MJPEG)
        return -EINVAL;
    if (!jpeg_rc_valid[chn])
        return -EAGAIN;
    mode = jpeg_rc_attr[chn][0];
    if ((rc = open_jpeg()))
        return rc;
    memset(&wire, 0, sizeof(wire));
    rc = call_ioctl(jpeg_fd, FH8626_MJPEG_GET_RC, &wire);
    if (rc)
        return rc;
    return jpeg_wire_rc_to_public(&wire, mode, attr);
}

int _JPEG_Start(void *opaque, uint32_t chn)
{
    int rc;
    (void)opaque;
    if (chn >= FH8626_JPEG_CHANNELS ||
        jpeg_mode[chn] != FH8626_JPEG_MODE_MJPEG)
        return -EINVAL;
    if ((rc = open_jpeg()))
        return rc;
    return call_ioctl(jpeg_fd, FH8626_JPEG_START, NULL);
}

int _JPEG_Stop(void *opaque, uint32_t chn)
{
    int rc;
    (void)opaque;
    if (chn >= FH8626_JPEG_CHANNELS ||
        jpeg_mode[chn] != FH8626_JPEG_MODE_MJPEG)
        return -EINVAL;
    if ((rc = open_jpeg()))
        return rc;
    return call_ioctl(jpeg_fd, FH8626_JPEG_STOP, NULL);
}

int _JPEG_SetRotate(void *opaque, uint32_t chn, uint32_t rotate)
{
    struct jpeg_rotate r;
    uint32_t mode;
    int rc;
    (void)opaque;

    if (chn >= FH8626_JPEG_CHANNELS || rotate > 3u ||
        !(mode = jpeg_mode[chn]))
        return -EINVAL;
    r = (struct jpeg_rotate){mode, rotate};
    if ((rc = open_jpeg()))
        return rc;
    rc = call_ioctl(jpeg_fd, FH8626_JPEG_SET_ROTATE, &r);
    if (!rc && jpeg_attr_valid[chn])
        jpeg_attr[chn][mode == FH8626_JPEG_MODE_SNAPSHOT ? 2 : 3] = rotate;
    return rc;
}

int _JPEG_GetRotate(void *opaque, uint32_t chn, uint32_t *rotate)
{
    struct jpeg_rotate r;
    uint32_t mode;
    int rc;
    (void)opaque;

    if (chn >= FH8626_JPEG_CHANNELS || !rotate ||
        !(mode = jpeg_mode[chn]))
        return -EINVAL;
    r = (struct jpeg_rotate){mode, 0};
    if ((rc = open_jpeg()))
        return rc;
    rc = call_ioctl(jpeg_fd, FH8626_JPEG_GET_ROTATE, &r);
    if (!rc)
        *rotate = r.rotate;
    return rc;
}

int _JPEG_HandleStream(const uint32_t *raw, uint32_t *out)
{
    uint32_t mode, chn;

    if (!raw || !out)
        return -EINVAL;
    mode = raw[1];
    if (mode == FH8626_JPEG_MODE_SNAPSHOT)
        chn = jpeg_snapshot_channel;
    else if (mode == FH8626_JPEG_MODE_MJPEG)
        chn = jpeg_mjpeg_channel;
    else
        return -ENOTSUP;
    if (chn == UINT32_MAX)
        return -EPIPE;

    out[0] = mode;
    out[1] = chn;
    out[2] = raw[6];
    out[3] = raw[7];
    out[4] = raw[8];
    out[5] = raw[9];
    out[6] = raw[10];
    return 0;
}

int _JPEG_ReleaseStream(void *opaque, uint32_t chn)
{
    uint32_t mode;
    int rc;
    (void)opaque;

    if (chn >= FH8626_JPEG_CHANNELS || !(mode = jpeg_mode[chn]))
        return -EINVAL;
    if ((rc = open_jpeg()))
        return rc;
    return call_ioctl(jpeg_fd, FH8626_JPEG_RELEASE, &mode);
}

int _JPEG_SubmitFrameEx(void *opaque, uint32_t chn,
                        const uint32_t *frame, uint32_t flags)
{
    uint32_t wire[12];
    uint32_t mode;
    int rc;
    (void)opaque;

    if (chn >= FH8626_JPEG_CHANNELS || !frame ||
        !(mode = jpeg_mode[chn]))
        return -EINVAL;

    /*
     * Recovered FH8852 public frame:
     *   [0] Y address
     *   [1] C address
     *   [2] auxiliary/PTS low
     *   [3] auxiliary/PTS high
     *   [4] width
     *   [5] height
     * plus the Ex-only frame/crop selector in r3.
     *
     * jpeg.ko expects a 12-word submit record. Words 9/10 are duplicated
     * width/height; the kernel normalizes them again in jpeg_usr_submit_frm.
     */
    memset(wire, 0, sizeof(wire));
    wire[0] = mode;
    wire[1] = frame[4];
    wire[2] = frame[5];
    wire[3] = frame[0];
    wire[4] = frame[1];
    wire[6] = frame[2];
    wire[7] = frame[3];
    wire[8] = flags;
    wire[9] = frame[4];
    wire[10] = frame[5];

    if ((rc = open_jpeg()))
        return rc;
    return call_ioctl(jpeg_fd, FH8626_JPEG_SUBMIT_FRAME, wire);
}

int _JPEG_SubmitFrame(void *opaque, uint32_t chn, const uint32_t *frame)
{
    return _JPEG_SubmitFrameEx(opaque, chn, frame, 0u);
}

int _JPEG_SetDropAttr(void *opaque, uint32_t chn, const uint32_t *attr)
{
    uint32_t wire[9];
    int rc;
    (void)opaque;

    if (chn >= FH8626_JPEG_CHANNELS || !attr ||
        jpeg_mode[chn] != FH8626_JPEG_MODE_MJPEG)
        return -EINVAL;

    /*
     * Exact donor public->kernel mapping. Public words 3 and 7 are padding /
     * unrelated fields for this request; packed frame ratios remain packed.
     */
    wire[0] = attr[0];
    wire[1] = attr[1];
    wire[2] = attr[2];
    wire[3] = attr[4];
    wire[4] = attr[5];
    wire[5] = attr[6];
    wire[6] = attr[8];
    wire[7] = attr[9];
    wire[8] = attr[10];

    if ((rc = open_jpeg()))
        return rc;
    return call_ioctl(jpeg_fd, FH8626_MJPEG_SET_DROP, wire);
}

int _JPEG_GetDropAttr(void *opaque, uint32_t chn, uint32_t *attr)
{
    uint32_t wire[9] = {0};
    int rc;
    (void)opaque;

    if (chn >= FH8626_JPEG_CHANNELS || !attr ||
        jpeg_mode[chn] != FH8626_JPEG_MODE_MJPEG)
        return -EINVAL;
    if ((rc = open_jpeg()))
        return rc;
    rc = call_ioctl(jpeg_fd, FH8626_MJPEG_GET_DROP, wire);
    if (rc)
        return rc;

    memset(attr, 0, 11u * sizeof(*attr));
    attr[0] = wire[0];
    attr[1] = wire[1];
    attr[2] = wire[2];
    attr[4] = wire[3];
    attr[5] = wire[4];
    attr[6] = wire[5];
    attr[8] = wire[6];
    attr[9] = wire[7];
    attr[10] = wire[8];
    return 0;
}

int _JPEG_GetHwAvgTime(void *opaque, uint32_t chn, uint32_t *out)
{
    uint32_t wire[4];
    uint32_t mode;
    int rc;
    (void)opaque;

    if (chn >= FH8626_JPEG_CHANNELS || !out ||
        !(mode = jpeg_mode[chn]))
        return -EINVAL;
    if ((rc = open_jpeg()))
        return rc;

    memset(wire, 0, sizeof(wire));
    wire[0] = mode;
    rc = call_ioctl(jpeg_fd, FH8626_JPEG_HW_AVG_TIME, wire);
    if (rc)
        return rc;

    /*
     * FH8852 public object has five reported values at words 0,1,4,5,6.
     * FH8626 jpeg.ko exposes only avg/max/window through its 0x10-byte wire.
     * Preserve the common metrics and explicitly zero unavailable extensions.
     */
    memset(out, 0, 7u * sizeof(*out));
    out[0] = wire[1]; /* average hardware time */
    out[1] = wire[2]; /* maximum hardware time */
    out[4] = wire[3]; /* averaging window */
    return 0;
}


/*
 * FH8626V100 stock enc.ko registers only media stream kind 4 (H.264) and the
 * H.264 PAE callback path. No HEVC encoder module/stream registration exists
 * in the retained AJL33PQ0866 stock stack. Never turn H.265 into a permissive
 * success: callers must see that this SoC/driver path does not provide it.
 */
int FH_VENC_GetH265Dblk(void) { return unsupported_feature("FH_VENC_GetH265Dblk"); }
int FH_VENC_GetH265IntraFresh(void) { return unsupported_feature("FH_VENC_GetH265IntraFresh"); }
int FH_VENC_GetH265SliceSplit(void) { return unsupported_feature("FH_VENC_GetH265SliceSplit"); }
int FH_VENC_SetH265Dblk(void) { return unsupported_feature("FH_VENC_SetH265Dblk"); }
int FH_VENC_SetH265IntraFresh(void) { return unsupported_feature("FH_VENC_SetH265IntraFresh"); }
int FH_VENC_SetH265SliceSplit(void) { return unsupported_feature("FH_VENC_SetH265SliceSplit"); }

/* Loader-complete optional FH8852 DSP/JPEG surface. */
SIMPLE_STUB0(FH_SYS_GetChipID)
SIMPLE_STUB0(FH_SYS_GetReg)
SIMPLE_STUB0(FH_SYS_GetVersion)
SIMPLE_STUB0(FH_SYS_GetVirtAddress)
SIMPLE_STUB0(FH_SYS_SetReg)
SIMPLE_STUB0(FH_VENC_GetDeBreathEffect)
SIMPLE_STUB0(FH_VENC_GetHwAvgTime)
SIMPLE_STUB0(FH_VENC_SetDeBreathEffect)
SIMPLE_STUB0(FH_VENC_SetEncryptSeed)
SIMPLE_STUB0(FH_VENC_Submit_ENC)
SIMPLE_STUB0(FH_VENC_Submit_ENC_Ex)
SIMPLE_STUB0(FH_VPSS_ClearMask)
SIMPLE_STUB0(FH_VPSS_CloseOsdtext)
SIMPLE_STUB0(FH_VPSS_ExportMallocedMem)
SIMPLE_STUB0(FH_VPSS_FrameBufferRegister)
SIMPLE_STUB0(FH_VPSS_FrameBufferUnRegister)
SIMPLE_STUB0(FH_VPSS_GetBGMData)
SIMPLE_STUB0(FH_VPSS_GetChnApcAttr)
SIMPLE_STUB0(FH_VPSS_GetChnCapality)
SIMPLE_STUB0(FH_VPSS_GetChnFrame)
SIMPLE_STUB0(FH_VPSS_GetChnFrameAdv)
SIMPLE_STUB0(FH_VPSS_GetChnFrameAdv_Ex)
SIMPLE_STUB0(FH_VPSS_GetChnFrame_Ex)
SIMPLE_STUB0(FH_VPSS_GetFrameBufferSize)
SIMPLE_STUB0(FH_VPSS_GetFrameRate)
SIMPLE_STUB0(FH_VPSS_GetGraph)
SIMPLE_STUB0(FH_VPSS_GetHwAvgTime)
SIMPLE_STUB0(FH_VPSS_GetMallocedMemBase)
SIMPLE_STUB0(FH_VPSS_GetMask)
SIMPLE_STUB0(FH_VPSS_GetOsd)
SIMPLE_STUB0(FH_VPSS_GetOsdHighlight)
SIMPLE_STUB0(FH_VPSS_GetOsdInvert)
SIMPLE_STUB0(FH_VPSS_GetRGBPreAttr)
SIMPLE_STUB0(FH_VPSS_GetUserPicAddr)
SIMPLE_STUB0(FH_VPSS_GetYCmeanMode)
SIMPLE_STUB0(FH_VPSS_ImportMallocedMem)
SIMPLE_STUB0(FH_VPSS_LOW_LATENCY_Disable)
SIMPLE_STUB0(FH_VPSS_LOW_LATENCY_Enable)
SIMPLE_STUB0(FH_VPSS_LockChnFrameAdv)
SIMPLE_STUB0(FH_VPSS_OpenOsdtext)
SIMPLE_STUB0(FH_VPSS_QueryUseCache)
SIMPLE_STUB0(FH_VPSS_ReadMallocedMem)
SIMPLE_STUB0(FH_VPSS_SendUserPic)
SIMPLE_STUB0(FH_VPSS_SetChn2LogoSel)
SIMPLE_STUB0(FH_VPSS_SetChnApcAttr)
SIMPLE_STUB0(FH_VPSS_SetDefaultScalerSize)
SIMPLE_STUB0(FH_VPSS_SetGraph)
SIMPLE_STUB0(FH_VPSS_SetMask)
SIMPLE_STUB0(FH_VPSS_SetOsd)
SIMPLE_STUB0(FH_VPSS_SetOsdAddr)
SIMPLE_STUB0(FH_VPSS_SetOsdHighlight)
SIMPLE_STUB0(FH_VPSS_SetOsdInvert)
SIMPLE_STUB0(FH_VPSS_SetRGBPreAttr)
SIMPLE_STUB0(FH_VPSS_UnlockChnFrameAdv)
SIMPLE_STUB0(FH_VPSS_WriteMallocedMem)
