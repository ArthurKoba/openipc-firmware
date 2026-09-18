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
 * VENC record layouts exposed by FH8852 Majestic are not yet fully recovered.
 * Those entry points are explicit staging boundaries: safe no-data behavior by
 * default, optional fixed 720p native bring-up behind
 * FH8626_MAJESTIC_NATIVE_VENC=1, and -ENOSYS in strict mode.
 */

#define FH8626_VMM_ALLOC          0xC0686D0AUL
#define FH8626_MEDIA_BIND         0xC0084D00UL
#define FH8626_MEDIA_UNBIND_SRC   0xC0044D02UL
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
#define FH8626_VPU_SET_FRAMECTRL  0xC0086954UL
#define FH8626_VPU_GET_FRAMECTRL  0xC0086955UL

#define FH8626_PAE_SYS_QUERY      0xC0045002UL
#define FH8626_PAE_SYS_INIT       0xC00C5000UL
#define FH8626_PAE_ENC_MEM_SIZE   0xC0145003UL
#define FH8626_PAE_ENC_MEM_INIT   0xC01C5004UL
#define FH8626_PAE_SET_CONFIG     0xC02C5006UL
#define FH8626_PAE_START_RECV     0xC0045008UL
#define FH8626_PAE_STOP_RECV      0xC0045009UL
#define FH8626_PAE_STREAM_STEP    0xC0045011UL
#define FH8626_PAE_FORCE_I        0xC0045014UL
#define FH8626_PAE_SET_RC         0xC054502FUL

#define FH8626_ISP_MMIO_PHYS      0xE8400000u
#define FH8626_ISP_MMIO_SIZE      0x4000u
#define FH8626_ISP_PRODUCER_MASK  0x000FFFFFu

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
static struct mem3 vpu_sys;
static struct mem3 vpu_chn[2];
static struct mem3 pae_sys;
static struct mem3 pae_chn;
static int pae_configured;
static int media_bound;
static int stream_lease_held;
static uint32_t stream_desc[FH8626_MEDIA_DESC_WORDS];

static int env_true(const char *name)
{
    const char *v = getenv(name);
    return v && v[0] && strcmp(v, "0");
}

static int strict_stub(const char *name)
{
    if (env_true("FH8626_MAJESTIC_TRACE"))
        fprintf(stderr, "fh8626-dsp-compat: stub %s\n", name);
    return env_true("FH8626_MAJESTIC_STUB_OK") ? 0 : -ENOSYS;
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

static void *map_phys(uint32_t phys, uint32_t size)
{
#if defined(__arm__)
    return (void *)(intptr_t)syscall(192, NULL, size, PROT_READ | PROT_WRITE,
                                     MAP_SHARED, vmm_fd, phys >> 12);
#else
    return mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, vmm_fd, phys);
#endif
}


static int producer_gate(int enable)
{
    int fd;
    volatile uint32_t *regs;
    uint32_t pending;

    if (!env_true("FH8626_MAJESTIC_NATIVE_VENC"))
        return 0;
    fd = open("/dev/mem", O_RDWR | O_SYNC | O_CLOEXEC);
    if (fd < 0)
        return -errno;
#if defined(__arm__)
    regs = (volatile uint32_t *)(intptr_t)syscall(
        192, NULL, FH8626_ISP_MMIO_SIZE, PROT_READ | PROT_WRITE,
        MAP_SHARED, fd, FH8626_ISP_MMIO_PHYS >> 12);
#else
    regs = mmap(NULL, FH8626_ISP_MMIO_SIZE, PROT_READ | PROT_WRITE,
                MAP_SHARED, fd, FH8626_ISP_MMIO_PHYS);
#endif
    if ((void *)regs == MAP_FAILED) {
        close(fd);
        return -errno;
    }
    if (enable) {
        pending = regs[0x004u / 4u];
        if (pending)
            regs[0x004u / 4u] = pending;
        regs[0x008u / 4u] = FH8626_ISP_PRODUCER_MASK;
    } else {
        regs[0x008u / 4u] = 0;
    }
    __sync_synchronize();
    munmap((void *)regs, FH8626_ISP_MMIO_SIZE);
    close(fd);
    return 0;
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
    uint32_t bind[2] = {1u, 7u};
    int rc;

    if (vpu_chn != 0u || venc_chn != 0u)
        return -ENOTSUP;
    if ((rc = open_native()))
        return rc;
    rc = call_ioctl(media_fd, FH8626_MEDIA_BIND, bind);
    if (!rc)
        media_bound = 1;
    return rc;
}

int FH_SYS_UnBindbySrc(uint32_t source)
{
    if (!media_bound)
        return 0;
    if (open_native())
        return -EIO;
    media_bound = 0;
    return call_ioctl(media_fd, FH8626_MEDIA_UNBIND_SRC, &source);
}

int FH_SYS_UnBindbyDst(uint32_t source) { return FH_SYS_UnBindbySrc(source); }
int FH_SYS_BindVpu2Bgm(void) { return strict_stub("FH_SYS_BindVpu2Bgm"); }
int FH_SYS_BindVpu2Nn(void) { return strict_stub("FH_SYS_BindVpu2Nn"); }
int FH_SYS_Set_Resource(void *p) { (void)p; return strict_stub("FH_SYS_Set_Resource"); }
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

int FH_VPSS_GetViAttr(void *attr)
{
    (void)attr;
    return strict_stub("FH_VPSS_GetViAttr");
}

int FH_VPSS_QueryChnMem(uint32_t chn, uint32_t width, uint32_t height,
                        uint32_t *size)
{
    struct vpu_query q = {chn, width, height, 0};
    int rc;

    if (!size || chn >= 2)
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

    if (chn >= 2)
        return -ENOTSUP;
    if (vpu_chn[chn].phys)
        return 0;
    rc = FH_VPSS_QueryChnMem(chn, width, height, &need);
    if (rc)
        return rc;
    rc = alloc_vmm(chn ? "majestic-vpu1" : "majestic-vpu0",
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

    trace_words("FH_VPSS_SetChnAttr", chn, attr, 12);
    if (!attr || chn >= 2)
        return -EINVAL;

    /*
     * FH8852 public attribute layout is not closed yet. In permissive bring-up
     * use the first two non-zero geometry words only when they are plausible;
     * otherwise keep the proven native 720p geometry.
     */
    cfg.chn = chn;
    cfg.width = FH8626_WIDTH;
    cfg.height = FH8626_HEIGHT;
    if (w[0] >= 32 && w[0] <= 1920 && w[1] >= 32 && w[1] <= 2048) {
        cfg.width = w[0];
        cfg.height = w[1];
    }
    if (open_native())
        return -EIO;
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
    (void)chn;
    return strict_stub("FH_VPSS_CloseChn");
}

int FH_VPSS_Enable(uint32_t chn)
{
    uint32_t enable = 1;
    int rc;

    if (chn != 0)
        return -ENOTSUP;
    if ((rc = open_native()))
        return rc;
    return call_ioctl(isp_fd, FH8626_VPU_ENABLE, &enable);
}

int FH_VPSS_Disable(uint32_t chn)
{
    uint32_t enable = 0;
    int rc;

    if (chn != 0)
        return -ENOTSUP;
    if ((rc = open_native()))
        return rc;
    return call_ioctl(isp_fd, FH8626_VPU_ENABLE, &enable);
}

int FH_VPSS_SetFramectrl(uint32_t chn, const uint32_t pair[2])
{
    /*
     * FH8852 SetFramectrl(chn,{N,D}) is not the same wire as the recovered
     * FH8626 {chn,packed_fps}. Keep it explicit until the conversion is proved.
     */
    trace_words("FH_VPSS_SetFramectrl", chn, pair, 2);
    return strict_stub("FH_VPSS_SetFramectrl");
}

int FH_VPSS_GetFramectrl(uint32_t chn, uint32_t pair[2])
{
    trace_words("FH_VPSS_GetFramectrl", chn, pair, 2);
    return strict_stub("FH_VPSS_GetFramectrl");
}

int FH_VPSS_FreezeVideo(void) { return 0; }
int FH_VPSS_UnfreezeVideo(void) { return 0; }

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
    if (chn != 0)
        return -ENOTSUP;
    trace_words("FH_VENC_CreateChn", chn, attr, attr ? 16 : 0);
    return env_true("FH8626_MAJESTIC_NATIVE_VENC") ? FH_VENC_SysInitMem()
                                                    : strict_stub("FH_VENC_CreateChn");
}

static int native_venc_fixed_720p(uint32_t chn)
{
    struct pae_mem_query q = {chn, 0, FH8626_WIDTH, FH8626_HEIGHT, 0};
    struct pae_mem mem;
    struct pae_cfg cfg;
    int rc;

    if (chn != 0)
        return -ENOTSUP;
    if ((rc = open_native()))
        return rc;
    if ((rc = FH_VENC_SysInitMem()))
        return rc;
    if (!pae_chn.phys) {
        rc = call_ioctl(pae_fd, FH8626_PAE_ENC_MEM_SIZE, &q);
        if (rc)
            return rc;
        rc = alloc_vmm("majestic-pae0", q.size, &pae_chn);
        if (rc)
            return rc;
        mem = (struct pae_mem){chn, pae_chn.phys, pae_chn.virt, pae_chn.size,
                               FH8626_WIDTH, FH8626_HEIGHT, 0};
        rc = call_ioctl(pae_fd, FH8626_PAE_ENC_MEM_INIT, &mem);
        if (rc)
            return rc;
    }
    cfg = (struct pae_cfg){chn, FH8626_WIDTH, FH8626_HEIGHT, 50, 66, 28,
                           FH8626_FPS_PACKED, 0, 0, 0, 0};
    rc = call_ioctl(pae_fd, FH8626_PAE_SET_CONFIG, &cfg);
    if (!rc) {
        struct pae_rc rate;
        const char *bitrate_env = getenv("FH8626_MAJESTIC_BITRATE_KBPS");
        uint32_t bitrate = bitrate_env && bitrate_env[0] ?
            (uint32_t)strtoul(bitrate_env, NULL, 0) : 4096u;

        if (!bitrate || bitrate > UINT32_MAX / 1000u)
            return -ERANGE;
        memset(&rate, 0, sizeof(rate));
        rate.chn = chn;
        rate.rc_mode = 0; /* recovered FH8626 VBR wire mode */
        rate.frame_rate_packed = FH8626_FPS_PACKED;
        rate.init_qp = 38;
        rate.bitrate_or_rate = bitrate * 1000u;
        rate.i_min_qp = 30; rate.i_max_qp = 50;
        rate.p_min_qp = 30; rate.p_max_qp = 50;
        rate.i_proportion = 5; rate.p_proportion = 1;
        rate.ip_qp_delta = 3;
        rate.max_rate_percent = 120;
        rate.still_rate_percent = 30;
        rate.max_still_qp = 38;
        rc = call_ioctl(pae_fd, FH8626_PAE_SET_RC, &rate);
    }
    if (!rc)
        pae_configured = 1;
    return rc;
}

/* FH8852 donor uses channel + pointer. Record layout remains unresolved. */
int FH_VENC_SetChnAttr(uint32_t chn, const void *attr)
{
    trace_words("FH_VENC_SetChnAttr", chn, attr, 24);
    if (env_true("FH8626_MAJESTIC_NATIVE_VENC"))
        return native_venc_fixed_720p(chn);
    return strict_stub("FH_VENC_SetChnAttr");
}

int FH_VENC_StartRecvPic(uint32_t chn)
{
    int rc;
    if (!env_true("FH8626_MAJESTIC_NATIVE_VENC"))
        return strict_stub("FH_VENC_StartRecvPic");
    if (!pae_configured)
        return -EPIPE;
    if ((rc = open_native()))
        return rc;
    rc = call_ioctl(pae_fd, FH8626_PAE_START_RECV, &chn);
    if (rc)
        return rc;
    rc = producer_gate(1);
    if (rc)
        (void)call_ioctl(pae_fd, FH8626_PAE_STOP_RECV, &chn);
    return rc;
}

int FH_VENC_StopRecvPic(uint32_t chn)
{
    uint32_t channel = 0;
    int rc, gate_rc, release_rc = 0;

    if (chn != 0)
        return -ENOTSUP;
    if ((rc = open_native()))
        return rc;

    /*
     * Native FH8626 teardown requires every encoded-stream lease to be
     * released before STOP_RECV. Preserve that invariant even when Majestic
     * stops a channel while its consumer still owns the last stream record.
     */
    if (stream_lease_held) {
        release_rc = call_ioctl(pae_fd, FH8626_PAE_STREAM_STEP, &channel);
        if (!release_rc)
            stream_lease_held = 0;
    }
    gate_rc = producer_gate(0);
    rc = call_ioctl(pae_fd, FH8626_PAE_STOP_RECV, &chn);
    if (release_rc)
        return release_rc;
    return rc ? rc : gate_rc;
}

int FH_VENC_RequestIDR(uint32_t chn)
{
    int rc;
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

    if (chn != 0 || !stream)
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
    rc = fill_public_stream(stream);
    if (rc) {
        uint32_t channel = 0;
        (void)call_ioctl(pae_fd, FH8626_PAE_STREAM_STEP, &channel);
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
    uint32_t channel = 0;
    int rc;

    (void)stream;
    if (chn != 0)
        return -EINVAL;
    if (!stream_lease_held)
        return -EPERM;
    if ((rc = open_native()))
        return rc;
    rc = call_ioctl(pae_fd, FH8626_PAE_STREAM_STEP, &channel);
    if (!rc)
        stream_lease_held = 0;
    return rc;
}

int FH_VENC_SetRCAttr(uint32_t chn, const void *attr)
{
    trace_words("FH_VENC_SetRCAttr", chn, attr, 24);
    return strict_stub("FH_VENC_SetRCAttr");
}

int FH_VENC_SetRcChangeParam(uint32_t chn, const void *attr)
{
    trace_words("FH_VENC_SetRcChangeParam", chn, attr, 8);
    return strict_stub("FH_VENC_SetRcChangeParam");
}

int FH_VENC_GetRCAttr(uint32_t chn, void *attr)
{
    trace_words("FH_VENC_GetRCAttr", chn, attr, 4);
    return -ENOSYS;
}

/* Loader/dlsym compatibility for common optional controls. */
#define SIMPLE_STUB0(name) int name(void) { return strict_stub(#name); }
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
SIMPLE_STUB0(FH_VENC_GetChnAttr)
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
