#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <dlfcn.h>
#include <sys/file.h>
#include <pthread.h>
#include <dirent.h>
#include "fh8626_algo_api.h"
#include "fh8626_sensor_gc1054.h"
#include "fh8626_isp_runtime.h"
#include "fh8626_nr3d_lifecycle.h"
#include "fh8626_scene_transaction.h"
#include "fh8626_control_status_tail.h"
#include "fh8626_lens_recovery.h"
#include "fh8626_ae_runtime.h"
#include "fh8626_stock_awb_mode0_pipeline.h"
#include "fh8626_stock_ca4f4_diag.h"
#include "fh8626_board_dualsensor.h"
#include "fh8626_sidecar_publisher.h"
#include "fh8626_log_rotation.h"
#include "fh8626_geometry_linux.h"
#include "fh8626_media_timing.h"
#include "../media_contracts/h264/fh8626_h264_rc.h"
#include "../media_contracts/h264/fh8626_h264_control.h"

#define VMM_ALLOC          0xC0686D0AUL
#define ISP_6905           0x80046905UL
#define ISP_6919           0x00006919UL
#define ISP_6920           0x40016920UL
#define ISP_6921           0x40016921UL
#define ISP_692F           0x4004692FUL
#define ISP_6932           0x00006932UL
#define ISP_START          0x0000690AUL
#define ISP_APPLY_GAMMA    0x40046924UL
#define ISP_STATS_READY    0x40016911UL
#define ISP_FRONT_STATUS   0x00006933UL
#define ISP_FRONT_TIMING   0x8010690EUL
#define ISP_FRAME_STATUS   0x40046930UL
#define ISP_CONTROL_MASK   0x4004692EUL
#define MEDIA_BIND         0xC0084D00UL
#define MEDIA_STREAM_6     0xC1704D06UL
#define VPU_MEM_QUERY      0xC0046942UL
#define VPU_SYS_MEM_INIT   0xC00C6940UL
#define VPU_SET_VI_ATTR    0xC0F46946UL
#define VPU_OPEN_CHN       0xC004694FUL
#define VPU_ENABLE         0xC004694DUL
#define PAE_SYS_QUERY      0xC0045002UL
#define PAE_SYS_INIT       0xC00C5000UL
#define PAE_ENC_MEM_SIZE   0xC0145003UL
#define PAE_ENC_MEM_INIT   0xC01C5004UL
#define PAE_SET_CONFIG     0xC02C5006UL
#define PAE_SET_RC_CONFIG  0xC054502FUL
#define PAE_GET_RC_CONFIG  0xC0545030UL
#define PAE_ENC_START      0xC0045008UL
#define PAE_ENC_STOP       0xC0045009UL
#define PAE_GET_STATUS     0xC030503FUL
#define PAE_STREAM_STEP    0xC0045011UL

#define CTL_PATH "/tmp/fh8626_ctl"
#define OWNER_LOCK_PATH "/var/run/fh8626_media_owner.lock"
#define CMD_QUEUE_CAP 32
#define CMD_LINE_CAP 256
#define ISP_MASK 0x000FFFFFU

struct mem3 { uint32_t phys, virt, size; };
struct pae_mem_q { uint32_t chn, size, width, height, refmode; };
struct pae_mem { uint32_t chn, phys, virt, size, width, height, refmode; };
struct pae_cfg {
    uint32_t chn, width, height, field0c, profile, qp, fps, mode;
    uint32_t field20, field24, field28;
};
/* Single canonical wire layout, including the formerly mislabelled tail. */
#define pae_rc_cfg fh_pae_rc_wire

struct app {
    int media, isp, pae, vmm, memfd, ctl, owner_lock;
    pthread_mutex_t algo_mu;
    pthread_mutex_t control_mu;
    pthread_t control_thread;
    int control_thread_started;
    volatile int control_thread_stop;
    uint64_t control_frames;
    uint32_t frame_status_retry;
    uint64_t algo_generation;
    char cmd_queue[CMD_QUEUE_CAP][CMD_LINE_CAP];
    unsigned cmd_head, cmd_tail, cmd_count, cmd_dropped;
    volatile uint32_t *r;
    struct mem3 imem, vsys, vch, psys, pch;
    struct fhg_session geometry;
    int geometry_startup_ready;
    int geometry_exclusive_open;
    struct fh_sensor_gc1054 sensor;
    struct fh8626_dualsensor_board dualsensor;
    struct fh_isp_runtime isp_rt;
    struct fh8626_ae_runtime ae_rt;
    char isp_profile[8];
    uint8_t sharpness_stock_detail[12], sharpness_stock_edge[12];
    uint32_t sharpness_level;
    uint8_t contrast_stock[12], brightness_stock[12], saturation_stock[12];
    uint32_t contrast_level, brightness_level, saturation_level;
    uint32_t nr3d_saved_468;
    int nr3d_disabled;
    int nr3d_reenable_pending;
    int running;
    int shutdown_requested;
    uint32_t venc_active_mask;
    uint32_t output_width, output_height;
    FILE *cap;
    unsigned cap_left;
    unsigned cap_frames;
    uint64_t cap_bytes;
    unsigned warmup_left;
    unsigned probe_left;
    unsigned cap_started;
    unsigned char sps[256], pps[128];
    unsigned sps_len, pps_len;
    uint64_t drain_frames;
    uint64_t stream_pts_us;
    uint32_t stream_last_ts_raw;
    int stream_have_ts;
    uint8_t e2_stats[0x90];
    uint32_t e2_stats_hash;
    uint32_t e2_stats_epoch;
    uint32_t awb_stats_epoch;
    int e2_stats_valid;
    int e2_ae_pending;
    struct fh8626_sidecar_publisher sidecar;
    uint64_t sidecar_publish_failures;
    uint64_t sidecar_non_annexb;
    uint64_t sidecar_prefix_bytes;
    unsigned lens_switches_ok;
    unsigned lens_switches_fail;
    unsigned lens_rollbacks;
    int lens_last_error;
    int lens_last_rollback_error;
    int lens_switch_busy;
    char scene[8];
    char ircut_state[8];
    int ir_led_state;
    int white_led_state;
    int light_sensor_value;
    int scene_auto_enabled;
    uint64_t scene_auto_next_ms;
    uint64_t scene_auto_last_switch_ms;
    uint64_t scene_log_next_ms;
    uint32_t scene_metric_history[10];
    uint32_t scene_metric_sum, scene_metric_index, scene_metric_count;
    uint64_t cap_limit;
    char cap_path[192];
    void *algo_dl;
    fh_algo_init_fn algo_init;
    fh_algo_tick_fn algo_tick;
    fh_algo_fini_fn algo_fini;
    struct fh_algo_host algo_host;
    char algo_path[192];
    int awb_mode0_enabled;
    int ae_auto_enabled;
    uint32_t ae_target, ae_last_mean;
    uint32_t ae_stock_target;
    uint32_t antiflicker_hz;
    uint32_t ae_settle_epoch;
    uint32_t ae_state_epoch;
    uint32_t ae_state_count;
    int ae_control_active;
    uint64_t ae_updates, ae_failures;
    int awb_last_rc;
    int awb_mode1_enabled;
    struct fh_stock_awb_mode0 awb_mode0;
    struct fh_stock_awb_snapshot awb_mode1_saved;
    uint32_t awb_mode1_saved_sensor_gain[3];
    int awb_mode1_saved_sensor_valid;
};

static void awb_mode1_step(struct app *a);
static int awb_mode1_save(struct app *a);
static void awb_mode1_restore(struct app *a);
static uint32_t app_ctx_u32(const uint8_t *p);

/* C3E74: CB970's first per-frame gate.  A frontend status of -1 suppresses
 * this epoch and advances the persistent retry counter up to timing word2;
 * any accepted status clears the counter.  Stock ignores dispatch returns;
 * production fails open on an ioctl error instead of branching on undefined
 * stack data. */
static int isp_frame_status_gate(struct app *a)
{
    int32_t status = 0;
    uint32_t timing[4] = {0u};
    if (!a || a->isp < 0) return -EINVAL;
    if (ioctl(a->isp, ISP_FRAME_STATUS, &status) != 0) {
        a->frame_status_retry = 0u;
        return 0;
    }
    (void)ioctl(a->isp, ISP_FRONT_TIMING, timing);
    if (status != -1) {
        a->frame_status_retry = 0u;
        return 0;
    }
    if (a->frame_status_retry < timing[2]) a->frame_status_retry++;
    else {
        a->frame_status_retry = 0u;
        printf("ISP frontend status remained -1 through retry window=%u\n", timing[2]);
    }
    return 1;
}

/* C64E0 + C4244: ioctl 0x80046905 returns the byte index applied to the
 * source descriptor whose owner allocation base is imem+0x148770.  Rebind
 * the runtime to that selected root before any C67C4/C6934 consumer. */
static int isp_select_stats_root(struct app *a)
{
    const uint32_t descriptor_base = 0x148770u;
    const size_t minimum_span = 0x2125cu;
    uint32_t selected = 0u;
    size_t root;
    int rc;
    if (!a || a->isp < 0) return -EINVAL;
    rc = ioctl(a->isp, ISP_6905, &selected);
    if (rc < 0) return -errno;
    root = (size_t)descriptor_base + (size_t)selected;
    if (root < descriptor_base || root > a->imem.size ||
        a->imem.size - root < minimum_span)
        return -ERANGE;
    return fh_isp_runtime_attach_isp_cfg(&a->isp_rt,
            (volatile uint8_t *)(uintptr_t)a->imem.virt + root,
            a->imem.size - root);
}

static int isp_frontend_sync_barrier(struct app *a)
{
    uint8_t ready_signal = 1u;
    uint32_t active = 0u, timing[4] = {0u};
    uint32_t saved68, delay_us = 80000u;
    int rc;
    if (!a || a->isp < 0) return -EINVAL;
    rc = ioctl(a->isp, ISP_STATS_READY, &ready_signal); /* C26F0 */
    if (rc) return rc;
    rc = ioctl(a->isp, ISP_FRONT_STATUS, &active);      /* C3F50 */
    if (rc) return rc;
    if (!active) return 0;
    if (ioctl(a->isp, ISP_FRONT_TIMING, timing) == 0) {
        uint32_t divisor = timing[2] ? timing[2] : 1u;
        delay_us = 15000u + 1000u * (1000u / divisor);
    }
    memcpy(&saved68, a->isp_rt.ctx + 0x68u, sizeof(saved68));
    saved68 &= ~1u;
    memcpy(a->isp_rt.ctx + 0x68u, &saved68, sizeof(saved68));
    usleep(delay_us);
    memset(a->isp_rt.ctx + 0x18u, 0, sizeof(uint32_t));
    usleep(delay_us);
    {
        uint32_t one = 1u;
        saved68 |= 1u;
        memcpy(a->isp_rt.ctx + 0x18u, &one, sizeof(one));
        memcpy(a->isp_rt.ctx + 0x68u, &saved68, sizeof(saved68));
    }
    return 0;
}
static int ae_auto_step(struct app *a);
static int ae_stock_runtime_step(struct app *a);

static int ae_frontend_timing(void *opaque, uint32_t timing[4])
{
    struct app *a=opaque;
    if (!a || a->isp<0 || !timing) return -EINVAL;
    return ioctl(a->isp,ISP_FRONT_TIMING,timing)==0?0:-errno;
}

static int ae_runtime_hook(void *opaque)
{
    struct app *a = opaque;
    int rc = 0;
    a->ae_rt.published_timing_slot = &a->isp_rt.ae_state.slot[4];
    a->ae_rt.published_action_slot = &a->isp_rt.ae_state.slot[8];
    pthread_mutex_lock(&a->algo_mu);
    if (a->isp_rt.ctx[0x2c] & 0x10u) {
        rc = fh8626_ae_runtime_special_step(&a->ae_rt);
    } else if (a->algo_tick) {
        uint64_t generation = a->algo_generation;
        rc = a->algo_tick(&a->algo_host, a->control_frames);
        if (generation != a->algo_generation)
            printf("ALGO generation changed during tick\n");
        if (rc)
            printf("ALGO tick rc=%d control_frame=%llu\n", rc,
                   (unsigned long long)a->control_frames);
    } else {
        rc = ae_stock_runtime_step(a);
    }
    pthread_mutex_unlock(&a->algo_mu);
    return rc;
}
static int kernel_nr3d_set(struct app *a,int enabled);
static int kernel_nr3d_request(void *opaque,int enabled);
static int nr3d_request(struct app *a,int enabled);
static int load_named_profile(struct app *a, const char *path, const char *name);

static void sharpness_capture_stock(struct app *a)
{
    memcpy(a->sharpness_stock_detail, a->isp_rt.ctx + 0x314u, 12u);
    memcpy(a->sharpness_stock_edge, a->isp_rt.ctx + 0x320u, 12u);
}

static void iq_capture_stock(struct app *a)
{
    sharpness_capture_stock(a);
    memcpy(a->contrast_stock, a->isp_rt.ctx + 0x2c0u, 12u);
    memcpy(a->brightness_stock, a->isp_rt.ctx + 0x2ccu, 12u);
    memcpy(a->saturation_stock, a->isp_rt.ctx + 0x2dcu, 12u);
}

static void iq_apply_row(uint8_t *dst, const uint8_t stock[12], uint32_t level)
{
    int delta = (int)level - 128;
    unsigned i;
    for (i = 0; i < 12u; ++i) {
        int value = (int)stock[i] + delta;
        if (value < 0) value = 0; else if (value > 255) value = 255;
        dst[i] = (uint8_t)value;
    }
}

static void sharpness_apply(struct app *a)
{
    int delta = (int)a->sharpness_level - 128;
    unsigned i;
    for (i = 0; i < 12u; ++i) {
        int detail = (int)a->sharpness_stock_detail[i] + delta;
        int edge = (int)a->sharpness_stock_edge[i] + delta;
        if (detail < 0) detail = 0; else if (detail > 255) detail = 255;
        if (edge < 0) edge = 0; else if (edge > 255) edge = 255;
        a->isp_rt.ctx[0x314u + i] = (uint8_t)detail;
        a->isp_rt.ctx[0x320u + i] = (uint8_t)edge;
    }
    a->isp_rt.params_dirty = 1;
}

static void iq_apply(struct app *a)
{
    sharpness_apply(a);
    iq_apply_row(a->isp_rt.ctx + 0x2c0u, a->contrast_stock, a->contrast_level);
    iq_apply_row(a->isp_rt.ctx + 0x2ccu, a->brightness_stock, a->brightness_level);
    iq_apply_row(a->isp_rt.ctx + 0x2dcu, a->saturation_stock, a->saturation_level);
    a->isp_rt.params_dirty = 1;
}

static int prepare_sensor_target(struct app *a, int target)
{
    uint32_t before = 0, after = 0;
    int rc;
    rc = fh8626_dualsensor_select(&a->dualsensor, target, &before, &after);
    printf("SENSOR_TARGET select=%s rc=%d before=%08x after=%08x\n",
           fh8626_dualsensor_name(target), rc, before, after);
    if (rc) return rc;
    usleep(600000);
    rc = fh_sensor_gc1054_init(&a->sensor);
    printf("SENSOR_TARGET init=%s rc=%d\n",
           fh8626_dualsensor_name(target), rc);
    if (rc) return rc;
    rc = fh_sensor_gc1054_set_fmt(&a->sensor, 0x801061A8u);
    printf("SENSOR_TARGET format=%s rc=%d fmt=801061a8\n",
           fh8626_dualsensor_name(target), rc);
    if (rc) return rc;
    usleep(600000);
    return 0;
}

static volatile sig_atomic_t want_safe_hold;

static int acquire_owner_lock(struct app *a)
{
    a->owner_lock = open(OWNER_LOCK_PATH, O_RDWR | O_CREAT, 0644);
    if (a->owner_lock < 0) { perror("open owner lock"); return -1; }
    if (flock(a->owner_lock, LOCK_EX | LOCK_NB) != 0) {
        fprintf(stderr, "FH8626 owner already active (%s): %s\n", OWNER_LOCK_PATH, strerror(errno));
        close(a->owner_lock); a->owner_lock = -1; return -1;
    }
    return 0;
}

static void on_signal(int sig)
{
    (void)sig;
    want_safe_hold = 1;
}

static void put32(unsigned char *p, unsigned off, uint32_t v) { memcpy(p + off, &v, 4); }
static uint32_t get32(unsigned char *p, unsigned off) { uint32_t v; memcpy(&v, p + off, 4); return v; }
static void wr(volatile uint32_t *r, unsigned off, uint32_t val) { r[off / 4] = val; }
static uint32_t rr(volatile uint32_t *r, unsigned off) { return r[off / 4]; }
static uint16_t app_ctx_u16(const uint8_t *p);

static int show(const char *name, int ret)
{
    printf("%-20s ret=%d hex=0x%08x errno=%d\n", name, ret, (uint32_t)ret, errno);
    fflush(stdout);
    return ret;
}

static void *fh_native_mmap2(void *addr, size_t len, int prot, int flags, int fd, uint32_t byte_offset)
{
    if (byte_offset & 0xFFFU) {
        errno = EINVAL;
        return MAP_FAILED;
    }
    return (void *)syscall(192, addr, len, prot, flags, fd, byte_offset >> 12);
}

static int alloc_vmm(int fd, const char *name, uint32_t need, struct mem3 *m)
{
    unsigned char req[104];
    void *p;
    int ret;
    if (!need || need > INT32_MAX-4095U || m->phys || m->virt || m->size)
        return -EINVAL;
    m->size = (need + 4095U) & ~4095U;
    memset(req, 0, sizeof(req));
    put32(req, 8, 4096);
    put32(req, 12, m->size);
    strncpy((char *)req + 28, name, 15);
    strncpy((char *)req + 44, "anonymous", 15);
    errno = 0;
    ret = ioctl(fd, VMM_ALLOC, req);
    printf("VMM %-12s ret=%d size=0x%x\n", name, ret, m->size);
    if (ret) return ret;
    m->phys = get32(req, 0);
    p = fh_native_mmap2(NULL, m->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, m->phys);
    if (p == MAP_FAILED) { printf("mmap %s failed: %s\n", name, strerror(errno)); return -1; }
    memset(p, 0, m->size);
    m->virt = (uint32_t)(uintptr_t)p;
    printf("    phys=0x%08x virt=0x%08x\n", m->phys, m->virt);
    return 0;
}

/* Only the never-started, single-output startup path can enter this adapter.
 * The process-wide owner flock excludes cooperating owners; control_mu excludes
 * this owner's runtime. A stopped/running=0 channel alone is NOT fresh memory. */
static int geometry_enter(void *opaque,const struct fhg_plan *p,enum fhg_phase phase)
{
    struct app *a=opaque;
    int rc=pthread_mutex_lock(&a->control_mu);
    if(rc)return -rc;
    if(a->owner_lock<0 || a->isp<0 || !a->geometry_startup_ready || !a->geometry_exclusive_open ||
       a->control_thread_started || a->running || a->venc_active_mask ||
       a->sidecar.initialized || a->geometry.state!=FHG_STATE_FRESH ||
       p->request.channel!=0 || p->request.native_width!=1280 ||
       p->request.native_height!=720 ||
       (phase!=FHG_PHASE_QUERY && phase!=FHG_PHASE_CONFIGURE_NEW)) {
        pthread_mutex_unlock(&a->control_mu);return -EBUSY;
    }
    /* Exact 244-byte GET_CHN_MEM: not initialized is 0x80094081, NOT
     * arbitrary ioctl failure. Reject any existing main/sub/cascade allocation.
     * A partial SET made by this process is separately fenced by session.state. */
    for(uint32_t ch=0;ch<3;ch++) {
        uint32_t wire[61]={0};wire[0]=ch;
        rc=ioctl(a->isp,0xc0f46945UL,wire);
        if((uint32_t)rc!=0x80094081u){
            pthread_mutex_unlock(&a->control_mu);return -EBUSY;
        }
    }
    if(phase==FHG_PHASE_CONFIGURE_NEW &&
       (!a->vch.phys || !a->vch.virt || !a->vch.size)) {
        pthread_mutex_unlock(&a->control_mu);return -EINVAL;
    }
    return 0;
}
static void geometry_leave(void *opaque)
{
    struct app *a=opaque;pthread_mutex_unlock(&a->control_mu);
}

/* A fresh userspace struct cannot certify a fresh global kernel channel.
 * Reject an ISP fd in another process before system/channel allocation. Under
 * the owner flock this establishes the supported single-owner startup scope.
 * A non-cooperating process opening the device later is not supported. */
static int geometry_exclusive_isp(struct app *a)
{
    struct stat mine,other;
    struct dirent *process,*entry;
    DIR *proc,*fds;
    char path[96],fdpath[384];
    int rc=0;
    if(fstat(a->isp,&mine) || !S_ISCHR(mine.st_mode))return -ENODEV;
    proc=opendir("/proc");if(!proc)return -errno;
    while((process=readdir(proc))!=NULL){
        char *end;unsigned long pid=strtoul(process->d_name,&end,10);
        if(!pid || *end)continue;
        snprintf(path,sizeof(path),"/proc/%lu/fd",pid);
        fds=opendir(path);
        if(!fds){if(errno==ENOENT)continue;rc=-errno;break;}
        while((entry=readdir(fds))!=NULL){
            if(entry->d_name[0]=='.')continue;
            snprintf(fdpath,sizeof(fdpath),"%s/%s",path,entry->d_name);
            if(stat(fdpath,&other)){
                if(errno==ENOENT)continue;
                rc=-errno;break;
            }
            if(S_ISCHR(other.st_mode) && other.st_rdev==mine.st_rdev){
                if(pid==(unsigned long)getpid() && strtol(entry->d_name,NULL,10)==a->isp)continue;
                rc=-EBUSY;break;
            }
        }
        closedir(fds);if(rc)break;
    }
    closedir(proc);return rc;
}

static int geometry_setup(struct app *a,uint32_t width,uint32_t height)
{
    struct fhg_request request=FHG_REQUEST_INITIALIZER;
    struct fhg_linux_context linux_ctx={a->isp,a,geometry_enter,geometry_leave};
    struct fhg_ops ops;
    struct fhg_requirements need;
    struct fhg_error error={0};
    struct fhg_memory memory;
    int rc;
    request.native_width=1280;request.native_height=720;
    request.visible_width=width;request.visible_height=height;
    /* Stock preset selectors, not percentages and not the CFB64 ISP LUTs.
     * Preserve the internal init row for the existing native720 preset. */
    request.coefficient=width==1920?13:(width==640?3:FHG_COEFF_INHERIT);
    rc=fhg_linux_make_ops(&linux_ctx,&ops);
    if(rc)return rc;
    rc=fhg_query_requirements(&ops,&request,&need,&error);
    if(rc)goto failed;
    rc=alloc_vmm(a->vmm,"vpu_ch0",need.bytes,&a->vch);
    if(rc)return rc;
    memory=(struct fhg_memory){a->vch.phys,a->vch.virt,a->vch.size};
    rc=fhg_configure_new_channel(&ops,&request,&memory,&a->geometry,&error);
    if(rc)goto failed;
    printf("GEOMETRY visible=%ux%u surface=%ux%u envelope=%ux%u bytes=%u/%u step=%08x/%08x coeff=%d ack=%u\n",
        width,height,a->geometry.plan.surface_width,a->geometry.plan.surface_height,
        a->geometry.plan.allocation_width,a->geometry.plan.allocation_height,
        need.bytes,memory.bytes,a->geometry.plan.scaler_step_x,
        a->geometry.plan.scaler_step_y,request.coefficient,
        a->geometry.coefficient_write_acknowledged);
    return 0;
failed:
    fprintf(stderr,"GEOMETRY failed rc=%d stage=%d request=%08x driver=%08x errno=%d retained=%d\n",
        rc,error.stage,error.ioctl_request,error.driver_result,error.system_errno,
        a->geometry.state!=FHG_STATE_FRESH);
    return rc;
}

static int dump_binary(const char *path, const void *buf, size_t len)
{
    FILE *f = fopen(path, "wb");
    size_t n;
    if (!f) { printf("DUMP open %s failed: %s\n", path, strerror(errno)); return -1; }
    n = fwrite(buf, 1, len, f);
    fclose(f);
    printf("DUMP %s bytes=%u wrote=%u\n", path, (unsigned)len, (unsigned)n);
    return n == len ? 0 : -1;
}

static void isp_regs_720p(volatile uint32_t *r)
{
    wr(r, 0x008, 0x00000000);
    wr(r, 0x018, 0x00000001);
    wr(r, 0x024, 0x0A61EB00);
    wr(r, 0x068, 0x00000005);
    wr(r, 0x028, 0x00002610);
    wr(r, 0x0DC, 0x00040000);
    wr(r, 0x0E0, 0x000C0008); wr(r, 0x0E4, 0x00140010); wr(r, 0x0E8, 0x001C0018);
    wr(r, 0x0EC, 0x00240020); wr(r, 0x0F0, 0x002C0028); wr(r, 0x0F4, 0x00400000);
    wr(r, 0x0F8, 0x00C00080); wr(r, 0x0FC, 0x01400100); wr(r, 0x100, 0x01C00180);
    wr(r, 0x104, 0x02400200); wr(r, 0x108, 0x02C00280);
    wr(r, 0x10C, 0x00100010); wr(r, 0x110, 0x00100010); wr(r, 0x114, 0x00100010);
    wr(r, 0x118, 0x00100010); wr(r, 0x11C, 0x00100010); wr(r, 0x120, 0x00100010);
    wr(r, 0x1F0, 0x00009015);
    wr(r, 0x2F8, 0x06420588); wr(r, 0x2FC, 0x07A706F7); wr(r, 0x300, 0x08FC0853);
    wr(r, 0x304, 0x0A4509A2); wr(r, 0x308, 0x0B840AE6); wr(r, 0x30C, 0x00000C1E);
    wr(r, 0x030, 0x02CF04FF); wr(r, 0x078, 0x02CF04FF); wr(r, 0x080, 0x02CF04FF);
    wr(r, 0x178, 0x00000044); wr(r, 0x17C, 0x00D40000); wr(r, 0x180, 0x027E01A9);
    wr(r, 0x184, 0x00770000); wr(r, 0x188, 0x016700EF);
    wr(r, 0x1A4, 0x00D40000); wr(r, 0x1A8, 0x027E01A9); wr(r, 0x1AC, 0x00770000);
    wr(r, 0x1B0, 0x016700EF); wr(r, 0x1CC, 0x0F0F1527); wr(r, 0x1D0, 0);
    wr(r, 0x1D4, 0x0001FFF1); wr(r, 0x1DC, 0x0F0F1527); wr(r, 0x1E0, 0);
    wr(r, 0x1E4, 0x0001FFF1); wr(r, 0x1EC, 0x15190000);
    wr(r, 0x38C, 0x00500000); wr(r, 0x390, 0x00F000A0); wr(r, 0x394, 0x01900140);
    wr(r, 0x398, 0x023001E0); wr(r, 0x39C, 0x0000027F); wr(r, 0x3A0, 0x00B4005A);
    wr(r, 0x3A4, 0x0167010E); wr(r, 0x3A8, 0x0FFF0000);
    wr(r, 0x488, 0x00000140); wr(r, 0x490, 0x00000640); wr(r, 0x5C0, 0);
    wr(r, 0x5C4, 0x02CF04FF);
}

static uint32_t crc32_buf(const unsigned char *p, unsigned len)
{
    uint32_t crc = 0xFFFFFFFFU;
    for (unsigned k = 0; k < len; k++) {
        crc ^= p[k];
        for (unsigned b = 0; b < 8; b++) crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
    return crc ^ 0xFFFFFFFFU;
}

static int find_start_code(const unsigned char *p, unsigned len, unsigned from, unsigned *sc, unsigned *sc_len)
{
    for (unsigned i = from; i + 3 < len; ++i) {
        if (p[i] == 0 && p[i+1] == 0 && p[i+2] == 1) { *sc=i; *sc_len=3; return 1; }
        if (i + 4 < len && p[i] == 0 && p[i+1] == 0 && p[i+2] == 0 && p[i+3] == 1) { *sc=i; *sc_len=4; return 1; }
    }
    return 0;
}

static int descriptor_has_nal(const unsigned char *p, unsigned len, unsigned want)
{
    unsigned pos=0, sc, sl;
    while (find_start_code(p,len,pos,&sc,&sl)) {
        unsigned n=sc+sl;
        if (n < len && (p[n] & 0x1fU) == want) return 1;
        pos=n+1;
    }
    return 0;
}

static void cache_parameter_sets(struct app *a, const unsigned char *p, unsigned len)
{
    unsigned pos=0, sc, sl;
    while (find_start_code(p,len,pos,&sc,&sl)) {
        unsigned n=sc+sl, next, nsl;
        if (n >= len) break;
        unsigned end=len;
        if (find_start_code(p,len,n+1,&next,&nsl)) end=next;
        unsigned type=p[n]&0x1fU, span=end-sc;
        if (type==7 && span<=sizeof(a->sps)) { memcpy(a->sps,p+sc,span); a->sps_len=span; printf("CACHE SPS len=%u\n",span); }
        if (type==8 && span<=sizeof(a->pps)) { memcpy(a->pps,p+sc,span); a->pps_len=span; printf("CACHE PPS len=%u\n",span); }
        pos=end;
    }
}

static int algo_set_intt(void *opaque, uint32_t v) { return fh_sensor_gc1054_set_intt((struct fh_sensor_gc1054 *)opaque, v); }
static int algo_set_gain(void *opaque, uint32_t v) { return fh_sensor_gc1054_set_gain((struct fh_sensor_gc1054 *)opaque, v); }
static int algo_write_reg(void *opaque, uint32_t reg, uint32_t v) { return fh_sensor_gc1054_write_reg((struct fh_sensor_gc1054 *)opaque, reg, v); }

static void algo_unload(struct app *a)
{
    pthread_mutex_lock(&a->algo_mu);
    a->algo_generation++;
    if (a->algo_fini) a->algo_fini(&a->algo_host);
    a->algo_init = NULL; a->algo_tick = NULL; a->algo_fini = NULL;
    if (a->algo_dl) dlclose(a->algo_dl);
    a->algo_dl = NULL;
    a->algo_path[0] = 0;
    pthread_mutex_unlock(&a->algo_mu);
}

static int algo_load(struct app *a, const char *path)
{
    void *dl;
    fh_algo_init_fn init = NULL;
    fh_algo_tick_fn tick = NULL;
    fh_algo_fini_fn fini = NULL;
    if (!path || !*path) path = "/tmp/libfhisp_algo.so";
    dl = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!dl) { printf("ALGO load %s failed: %s\n", path, dlerror()); return -1; }
    *(void **)(&init) = dlsym(dl, "fh_algo_init");
    *(void **)(&tick) = dlsym(dl, "fh_algo_tick");
    *(void **)(&fini) = dlsym(dl, "fh_algo_fini");
    if (!init || !tick) { printf("ALGO %s missing init/tick\n", path); dlclose(dl); return -1; }
    pthread_mutex_lock(&a->algo_mu);
    a->algo_generation++;
    if (a->algo_fini) a->algo_fini(&a->algo_host);
    a->algo_init = NULL; a->algo_tick = NULL; a->algo_fini = NULL;
    if (a->algo_dl) dlclose(a->algo_dl);
    a->algo_dl = NULL;
    a->algo_dl = dl; a->algo_init = init; a->algo_tick = tick; a->algo_fini = fini;
    snprintf(a->algo_path, sizeof(a->algo_path), "%s", path);
    if (a->algo_init(&a->algo_host) != 0) {
        printf("ALGO init failed %s\n", path);
        a->algo_init = NULL; a->algo_tick = NULL; a->algo_fini = NULL;
        dlclose(a->algo_dl); a->algo_dl = NULL; a->algo_path[0] = 0;
        pthread_mutex_unlock(&a->algo_mu); return -1;
    }
    pthread_mutex_unlock(&a->algo_mu);
    printf("ALGO loaded %s abi=%u\n", a->algo_path, a->algo_host.abi_version);
    return 0;
}

static void capture_close(struct app *a)
{
    if (!a->cap) return;
    fclose(a->cap);
    a->cap = NULL;
    sync();
    printf("CAPTURE DONE frames=%u bytes=%llu file=%s\n", a->cap_frames,
           (unsigned long long)a->cap_bytes, a->cap_path);
    a->cap_left = 0;
}

static int capture_open(struct app *a, unsigned frames, const char *path)
{
    capture_close(a);
    if (!frames) frames = 50;
    if (!path || !*path) path = "/tmp/capture.h264";
    snprintf(a->cap_path, sizeof(a->cap_path), "%s", path);
    a->cap = fopen(a->cap_path, "wb");
    if (!a->cap) { printf("capture open %s failed: %s\n", a->cap_path, strerror(errno)); return -1; }
    a->cap_left = frames;
    a->cap_frames = 0;
    a->cap_bytes = 0;
    a->cap_started = 0;
    if (!a->cap_limit) a->cap_limit = 8ULL * 1024ULL * 1024ULL;
    printf("CAPTURE START frames=%u file=%s wait_idr=1 cached_sps=%u cached_pps=%u\n", frames, a->cap_path, a->sps_len, a->pps_len);
    return 0;
}

static void awb_init_triplet(void *opaque,const uint32_t triplet[3])
{
    struct app *a=opaque;
    fh_stock_awb_init_triplet(&a->awb_mode0,triplet);
}

static int awb_runtime_hook(void *opaque)
{
    struct app *a = opaque;
    int rc;
    if (!a) return -EINVAL;
    if (!a->e2_stats_valid || a->awb_stats_epoch == a->e2_stats_epoch) return 0;
    a->awb_stats_epoch = a->e2_stats_epoch;
    a->awb_mode0.sensor_gain = fh_sensor_gc1054_awb_gain;
    a->awb_mode0.sensor_query = fh_sensor_gc1054_awb_query;
    a->awb_mode0.sensor_gain_opaque = &a->sensor;
    a->awb_mode0.mode1_paused = !a->awb_mode1_enabled;
    /* One shared CB5D4 owns epoch, neutral/normal branches, CA428,
     * estimator publication, CB4F0 ratios and C9F68/5D4 tail. CE764 stays
     * in the enclosing CB970 late slot. Automatic ticks do not create a
     * stale manual-rollback snapshot. */
    rc = fh_stock_awb_dispatch(&a->awb_mode0,a->isp_rt.ctx,a->r,a->e2_stats,NULL);
    a->awb_last_rc = rc;
    return rc;
}

/* Stock CB970 is driven by the ISP frontend cycle, not by availability of an
 * encoded access unit. Keep the complete statistics/control transaction under
 * one owner lock and run it from the dedicated control thread below. */
static int control_mask_query(void *opaque,uint32_t *mask)
{
    struct app *a=opaque;
    return ioctl(a->isp,ISP_CONTROL_MASK,mask)==0?0:-(errno?errno:EIO);
}

static int control_tail_and_awb_hook(void *opaque)
{
    struct app *a = opaque;
    a->isp_rt.last_control_tail_error=0;
    /* C9240 returns before the normal status tail; AWB remains a CB970 stage. */
    if (!((a->isp_rt.ctx[0x10] & 1u) && (a->isp_rt.ctx[0x2c] & 0x10u))) {
        (void)fh_control_status_tail(&a->isp_rt,a->ae_rt.gate.state,
                                     control_mask_query,ae_frontend_timing,a);
    }
    return awb_runtime_hook(a);
}

static int isp_control_once(struct app *a)
{
    uint8_t first[0x90], second[0x90];
    uint32_t h = 2166136261u;
    unsigned i;
    int rc;

    if (!a->running) return 0;
    rc = isp_frame_status_gate(a);          /* C3E74 exact CB970 gate */
    if (rc < 0) return rc;
    if (rc > 0) return -EAGAIN;
    rc = isp_select_stats_root(a);          /* C64E0/C4244 selected descriptor */
    if (rc) return rc;
    rc = isp_frontend_sync_barrier(a);
    if (rc) return rc;
    if (!a->isp_rt.isp_cfg || a->isp_rt.isp_cfg_size < 0x48u + sizeof(first)) return -ERANGE;
    memcpy(first, (const void *)(uintptr_t)(a->isp_rt.isp_cfg + 0x48u), sizeof(first));
    __sync_synchronize();
    memcpy(second, (const void *)(uintptr_t)(a->isp_rt.isp_cfg + 0x48u), sizeof(second));
    if (memcmp(first, second, sizeof(first))) return -EAGAIN;
    for (i = 0; i < sizeof(first); ++i) h = (h ^ first[i]) * 16777619u;
    memcpy(a->e2_stats, first, sizeof(first));
    a->e2_stats_hash = h;
    if (++a->e2_stats_epoch == 0u) a->e2_stats_epoch = 1u;
    a->e2_stats_valid = 1;
    a->e2_ae_pending = 1;
    fh_isp_runtime_accept_stats_epoch(&a->isp_rt, a->e2_stats_epoch);

    a->isp_rt.control_q8_aux = a->ae_rt.q8_aux;
    rc = fh_isp_runtime_tick_with_control_hooks(&a->isp_rt,
                                                ae_runtime_hook, a,
                                                control_tail_and_awb_hook, a);
    a->control_frames++; /* accepted cycles, including failed control steps */
    if (a->isp_rt.last_control_tail_error && (a->control_frames<=8u || !(a->control_frames%25u)))
        printf("STOCKRUNTIME control tail rc=%d (independent slots/AWB continued)\n",
               a->isp_rt.last_control_tail_error);
    if (a->isp_rt.stage_error_count && (a->control_frames<=8u || !(a->control_frames%25u)))
        printf("STOCKRUNTIME late stage=%05x rc=%d rejected=%u (independent stages continued)\n",
               a->isp_rt.last_stage_address,a->isp_rt.last_stage_error,
               a->isp_rt.stage_error_count);
    if (a->isp_rt.last_awb_error && (a->control_frames<=8u || !(a->control_frames%25u)))
        printf("STOCKRUNTIME AWB rc=%d control_frame=%llu (late ISP continued)\n",
               a->isp_rt.last_awb_error,(unsigned long long)a->control_frames);
    if (a->isp_rt.last_ae_error && (a->control_frames<=8u || !(a->control_frames%25u)))
        printf("STOCKRUNTIME AE rc=%d control_frame=%llu (late ISP continued)\n",
               a->isp_rt.last_ae_error, (unsigned long long)a->control_frames);
    if (a->isp_rt.last_ae_log_error && (a->control_frames<=8u || !(a->control_frames%25u)))
        printf("STOCKRUNTIME AELOG rc=%d state=%u action=%u (late ISP continued)\n",
               a->isp_rt.last_ae_log_error,a->isp_rt.ae_state.slot[7],a->isp_rt.ae_state.slot[8]);
    if (!rc && app_ctx_u32(a->isp_rt.ctx + 0x11a8u) == 1u)
        (void)ioctl(a->isp, ISP_APPLY_GAMMA, a->isp_rt.ctx + 0x0f20u);
    if (rc) {
        if (a->control_frames<=8u || !(a->control_frames%25u))
            printf("STOCKRUNTIME tick rc=%d control_frame=%llu\n", rc,
                   (unsigned long long)a->control_frames);
        return rc;
    }
    if (a->control_frames <= 8u) {
        printf("STOCKRUNTIME control_frame=%llu 454=%08x 464=%08x 4b8=%08x 4f4=%08x 520=%08x 524=%08x\n",
               (unsigned long long)a->control_frames, rr(a->r,0x454), rr(a->r,0x464), rr(a->r,0x4b8), rr(a->r,0x4f4), rr(a->r,0x520), rr(a->r,0x524));
    }
    if (a->nr3d_reenable_pending && fh_isp_runtime_nr3d_ready(&a->isp_rt)) {
        int nrc=fh_nr3d_finish_reenable(&a->isp_rt,&a->nr3d_reenable_pending,
                                       kernel_nr3d_request,a);
        if(!nrc || a->control_frames<=8u || !(a->control_frames%25u))
        printf("NR3D_RESEED generation=%u epoch=%u kernel_rc=%d pending=%d\n",
               a->isp_rt.profile_generation, a->isp_rt.gain_epoch,
               nrc, a->nr3d_reenable_pending);
    }
    return 0;
}

static void *isp_control_thread_main(void *opaque)
{
    struct app *a = opaque;
    struct timespec deadline;
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    while (!a->control_thread_stop) {
        int rc;
        struct timespec now;
        pthread_mutex_lock(&a->control_mu);
        rc = isp_control_once(a);
        pthread_mutex_unlock(&a->control_mu);
        if (rc && rc != -EAGAIN) usleep(1000u);
        deadline.tv_nsec += 40000000L;
        if (deadline.tv_nsec >= 1000000000L) {
            deadline.tv_sec++;
            deadline.tv_nsec -= 1000000000L;
        }
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (deadline.tv_sec < now.tv_sec ||
            (deadline.tv_sec == now.tv_sec && deadline.tv_nsec < now.tv_nsec))
            deadline = now; /* never burst to catch up after a long barrier */
        (void)clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, NULL);
    }
    return NULL;
}

static int stream_once(struct app *a)
{
    uint32_t s[92];
    int ret;
    uint32_t virt, len;
    unsigned char *p = NULL, *tmp = NULL;
    uint32_t ring_base = a->psys.virt, ring_size = a->psys.size;
    uint64_t pts_us;
    fh8626_sidecar_publisher_service(&a->sidecar);
    memset(s, 0, sizeof(s));
    s[0] = 4;
    ret = ioctl(a->media, MEDIA_STREAM_6, s);
    if (ret != 0 || s[1] != 4) return ret;
    virt = s[7];
    len = s[8];
    uint64_t ring_end = (uint64_t)ring_base + (uint64_t)ring_size;
    if (!len || (uint64_t)len > (uint64_t)ring_size ||
        (uint64_t)virt < (uint64_t)ring_base || (uint64_t)virt >= ring_end ||
        ring_end > UINT32_MAX + UINT64_C(1)) {
        printf("BAD STREAM DESC virt=%08x len=%u base=%08x size=%08x -> RELEASE\n", virt, len, ring_base, ring_size);
        goto release_stream;
    }
    {
        uint32_t tail = (uint32_t)(ring_end - (uint64_t)virt);
        if (len <= tail) {
            p = (unsigned char *)(uintptr_t)virt;
        } else {
            uint32_t head = len - tail;
            tmp = malloc(len);
            if (!tmp) { printf("STREAM WRAP malloc len=%u failed -> RELEASE\n", len); goto release_stream; }
            memcpy(tmp, (const void *)(uintptr_t)virt, tail);
            memcpy(tmp + tail, (const void *)(uintptr_t)ring_base, head);
            p = tmp;
            printf("STREAM WRAP virt=%08x len=%u tail=%u head=%u\n", virt, len, tail, head);
        }
    }
    cache_parameter_sets(a, p, len);
    if (!a->stream_have_ts) {
        a->stream_have_ts = 1;
        a->stream_last_ts_raw = s[10];
        a->stream_pts_us = 0;
    } else {
        uint32_t delta = s[10] - a->stream_last_ts_raw;
        /* Target probe proves descriptor word 10 is a wrapping microsecond
         * clock (~40000 per 25-fps access unit). Reject discontinuities caused
         * by encoder restart instead of projecting them into the frontend. */
        if (!delta || delta > 1000000u) delta = 40000u;
        a->stream_pts_us += delta;
        a->stream_last_ts_raw = s[10];
    }
    pts_us = a->stream_pts_us;
    if (a->sidecar.initialized) {
        unsigned sc = 0, sc_len = 0;
        if (find_start_code(p, len, 0, &sc, &sc_len)) {
            int publish_rc;
            (void)sc_len;
            a->sidecar_prefix_bytes += sc;
            if(descriptor_has_nal(p+sc,len-sc,5)) {
                int has_sps=descriptor_has_nal(p+sc,len-sc,7);
                int has_pps=descriptor_has_nal(p+sc,len-sc,8);
                /* Startup/reconnect must begin with decoder-ready headers,
                 * including when the driver sends them in separate descriptors. */
                if((!has_sps && !a->sps_len) || (!has_pps && !a->pps_len))
                    publish_rc=0;
                else publish_rc=fh8626_sidecar_publisher_publish_idr(&a->sidecar,
                    p+sc,len-sc,pts_us,a->sps,(has_sps && has_pps)?0:a->sps_len,
                    a->pps,(has_sps && has_pps)?0:a->pps_len);
            } else publish_rc=fh8626_sidecar_publisher_publish(&a->sidecar,
                p+sc,len-sc,pts_us,0);
            if (publish_rc < 0) {
                a->sidecar_publish_failures++;
                if (a->sidecar_publish_failures <= 8)
                    printf("SIDECAR publish failed len=%u errno=%d failures=%llu\n",
                           len - sc, errno,
                           (unsigned long long)a->sidecar_publish_failures);
            }
        } else {
            a->sidecar_non_annexb++;
        }
    }
    if (a->warmup_left) {
        a->warmup_left--;
        printf("WARMUP drop phys=%08x len=%u ts=%08x left=%u\n", s[6], len, s[10], a->warmup_left);
    } else if (a->probe_left) {
        uint32_t crc = crc32_buf(p, len);
        printf("PROBE frame=%llu phys=%08x virt=%08x len=%u ts=%08x crc32=%08x left=%u\n",
               (unsigned long long)a->drain_frames, s[6], virt, len, s[10], crc, a->probe_left - 1);
        a->probe_left--;
    } else if (a->cap) {
        uint32_t crc = crc32_buf(p, len);
        if (!a->cap_started) {
            if (!descriptor_has_nal(p, len, 5)) {
                printf("CAP WAIT IDR phys=%08x len=%u ts=%08x\n", s[6], len, s[10]);
                goto release_stream;
            }
            if (!descriptor_has_nal(p,len,7) && a->sps_len) { fwrite(a->sps,1,a->sps_len,a->cap); a->cap_bytes += a->sps_len; }
            if (!descriptor_has_nal(p,len,8) && a->pps_len) { fwrite(a->pps,1,a->pps_len,a->cap); a->cap_bytes += a->pps_len; }
            a->cap_started = 1;
            printf("CAP IDR START cached_sps=%u cached_pps=%u\n", a->sps_len, a->pps_len);
        }
        if ((uint64_t)len > a->cap_limit || a->cap_bytes > a->cap_limit - (uint64_t)len) {
            printf("CAPTURE LIMIT hit bytes=%llu next=%u limit=%llu\n",
                   (unsigned long long)a->cap_bytes, len, (unsigned long long)a->cap_limit);
            capture_close(a);
        } else if (fwrite(p, 1, len, a->cap) != len) {
            perror("fwrite"); capture_close(a);
        } else {
            printf("CAP frame=%u phys=%08x virt=%08x len=%u ts=%08x crc32=%08x\n", a->cap_frames, s[6], virt, len, s[10], crc);
            a->cap_frames++;
            a->cap_bytes += len;
            if (a->cap_left) a->cap_left--;
        }
    }
release_stream:
    if (tmp) free(tmp);
    {
        uint32_t ch = 0;
        int pret = ioctl(a->pae, PAE_STREAM_STEP, &ch);
        if (pret) printf("PAE_RELEASE ret=%d ch=%u\n", pret, ch);
    }
    a->drain_frames++;
    if (a->cap && a->cap_left == 0) capture_close(a);
    return 0;
}

static uint16_t app_ctx_u16(const uint8_t *p)
{
    uint16_t v;
    memcpy(&v, p, sizeof(v));
    return v;
}

static uint32_t antiflicker_quantum_lines(const struct app *a)
{
    uint32_t vts, hz;
    if (!a || !a->antiflicker_hz) return 1u;
    hz = a->antiflicker_hz == 1u ? a->isp_rt.ctx[0xa4c] : a->antiflicker_hz;
    if (!hz) return 1u;
    vts = app_ctx_u16(a->isp_rt.ctx + 0x1a);
    if (!vts) vts = 899u;
    /* C7264 requests 500000/selector microseconds: a mains half-cycle. */
    return (vts * 25u + hz) / (2u * hz);
}

static uint32_t antiflicker_quantize(const struct app *a, uint32_t value,
                                     uint32_t limit, int increasing)
{
    uint32_t q = antiflicker_quantum_lines(a), n;
    if (q <= 1u) return value > limit ? limit : value;
    /* Bright-scene exposure shorter than one mains half-cycle cannot be
     * quantized to a full quantum: doing so pins integration at q and leaves
     * AE permanently overexposed. Flicker locking applies to long exposures;
     * preserve the requested sub-quantum integration on the bright branch. */
    if (!increasing && value < q) return value > limit ? limit : value;
    n = increasing ? ((value + q - 1u) / q) * q : (value / q) * q;
    if (!n) n = q;
    if (n > limit) n = (limit / q) * q;
    return n ? n : (limit < q ? limit : q);
}

/* Exact C7EB0 negative-selector correction factor.  The shipped profiles use
 * ctx+0x3c as the smoothing control and C883C consumes this IEEE-754 value for
 * INTT/AGAIN/DGAIN actions. */
static float ae_c7eb0_factor(uint32_t measured, uint32_t target, uint8_t smoothing)
{
    float denom = (float)((uint32_t)smoothing + 1u);
    float m = (float)(measured > 1u ? measured : 1u);
    float ratio = (float)target / m;
    float factor;
    if (ratio > 1.0f) {
        denom += 1.0f;
        if ((double)ratio <= 1.2) {
            factor = 1.0f - ((1.0f - ratio) / denom);
            if (factor >= 1.0078125f) factor = 1.0078125f;
            else if (factor < 1.0f) factor = 1.0f;
        } else {
            factor = 1.0f - ((1.0f - ratio) / (denom + 2.0f));
            if (factor >= 1.5f) factor = 1.5f;
            else if (factor < 1.0078125f) factor = 1.0078125f;
        }
    } else if ((double)ratio < 0.7) {
        factor = 1.0f - ((1.0f - ratio) / denom);
        if (factor >= 0.9921875f) factor = 0.9921875f;
        else if ((double)factor < 0.6) factor = 0.6f;
    } else {
        factor = 1.0f - ((1.0f - ratio) / (denom + 2.0f));
        if (factor >= 1.0f) factor = 1.0f;
        else if (factor < 0.9921875f) factor = 0.9921875f;
    }
    return factor;
}

/* C6D70 stock guard: keep the configured integration ceiling five lines below
 * the active sensor-mode frame limit when the profile ceiling reaches it. */
static uint32_t ae_effective_intt_max(const struct app *a)
{
    uint32_t frame_limit = app_ctx_u16(a->isp_rt.ctx + 0x1a);
    uint32_t effective = app_ctx_u16(a->isp_rt.ctx + 0x38);
    if (frame_limit <= 5u) {
        if (effective >= frame_limit) effective = 0u;
    } else if (effective >= frame_limit - 4u) {
        effective = frame_limit - 5u;
    }
    return effective;
}

static void ae_apply_stock_intt_guard(struct app *a)
{
    uint16_t effective = (uint16_t)ae_effective_intt_max(a);
    memcpy(a->isp_rt.ctx + 0x38, &effective, sizeof(effective));
}

static void ae_refresh_profile_policy(struct app *a)
{
    uint32_t old_stock = a->ae_stock_target;
    uint32_t new_stock = (uint32_t)a->isp_rt.ctx[0x30u] << 4;
    if (!new_stock) new_stock = 1280u;
    a->ae_stock_target = new_stock;
    if (!a->ae_target || a->ae_target == old_stock) a->ae_target = new_stock;
    /* Profile bytes live in the existing ISP context.  Stock does not rerun
     * C9740/C9868 or recreate C949C's separate runtime object when those bytes
     * change, so preserve C6D04 history, hysteresis, Q8 actuator state and the
     * takeover rollback snapshot across day/night/wlight profile loads. */
    if (!a->ae_rt.ctx) {
        fh8626_ae_runtime_init_passive(&a->ae_rt, a->isp_rt.ctx, a->r,
                                       &a->sensor, NULL, NULL, ae_frontend_timing, a);
    } else {
        a->ae_rt.ctx = a->isp_rt.ctx;
        a->ae_rt.isp_mmio = a->r;
        a->ae_rt.sensor = &a->sensor;
    }
    (void)fh8626_ae_runtime_enable_observe(&a->ae_rt, 1);
    (void)fh8626_ae_runtime_enable_commit(&a->ae_rt, 1);
}

static uint32_t app_ctx_u32(const uint8_t *p)
{
    uint32_t v;
    memcpy(&v, p, sizeof(v));
    return v;
}

static void awb_mode0_enable(struct app *a)
{
    int rc;
    if (a->awb_mode0_enabled) { printf("AWBMODE0 already on\n"); return; }
    if (a->awb_mode1_saved.valid) {
        printf("AWBMODE0 another manual trial is saved; restore it first\n");
        return;
    }
    rc=awb_mode1_save(a);
    if (rc) { printf("AWBMODE0 snapshot unavailable rc=%d\n",rc); return; }
    /* A mode switch retains shared logical/history state, rather than
     * memset-reseeding a second unrelated AWB controller. */
    a->isp_rt.ctx[0x6c] &= (uint8_t)~3u;
    a->isp_rt.ctx[0x10] |= 2u;
    a->awb_mode0_enabled=1;
    printf("AWBMODE0 on shared-state=1 mode=%u\n",a->isp_rt.ctx[0x6c]&3u);
}

static void awb_mode0_disable(struct app *a)
{
    if (!a->awb_mode0_enabled) { printf("AWBMODE0 no manual trial\n"); return; }
    awb_mode1_restore(a);
    if (!a->awb_mode1_saved.valid) a->awb_mode0_enabled=0;
}

static void set_running(struct app *a, int on)
{
    if (on) {
        uint32_t pending = rr(a->r, 0x004);
        if (pending) wr(a->r, 0x004, pending);
        wr(a->r, 0x008, ISP_MASK);
        a->running = 1;
        if (a->warmup_left < 5) a->warmup_left = 5;
        printf("RUN mask=%08x warmup=%u\n", rr(a->r, 0x008), a->warmup_left);
    } else {
        wr(a->r, 0x008, 0);
        a->running = 0;
        capture_close(a);
        printf("STOP/HOLD mask=%08x status=%08x owner-kept=1\n", rr(a->r, 0x008), rr(a->r, 0x004));
    }
}

static int venc_set_started(struct app *a, uint32_t mask, int started)
{
    unsigned ch;
    int rc = 0;

    for (ch = 0; ch < 3; ++ch) {
        uint32_t c = ch;
        int r;
        if (!(mask & (1u << ch))) continue;
        errno = 0;
        r = ioctl(a->pae, started ? PAE_ENC_START : PAE_ENC_STOP, &c);
        printf("LENS PAE_ENC_%s ch=%u rc=%d errno=%d\n",
               started ? "START" : "STOP", ch, r, errno);
        if (r && !rc) rc = errno ? -errno : -EIO;
    }
    return rc;
}

static int owner_h264_ioctl(void *opaque, unsigned long req, void *arg)
{
    struct app *a = opaque;
    int rc;
    errno = 0;
    rc = ioctl(a->pae, req, arg);
    return rc ? -(errno ? errno : EIO) : 0;
}

static int encoder_rc_get(struct app *a, struct pae_rc_cfg *rc)
{
    struct fh_h264_control control = {owner_h264_ioctl, a, 0};
    if (!a || !rc) return -EINVAL;
    return fh_h264_get_rc(&control, rc);
}

static void encoder_rc_default_main(struct pae_rc_cfg *rc)
{
    memset(rc, 0, sizeof(*rc));
    rc->chn = 0u;
    rc->rc_mode = FH_PAE_RC_CBR; /* PAE wire domain, not Apollo app enum. */
    rc->frame_rate_packed = FH8626_OWNER_FPS_PACKED;
    rc->init_qp = 38u;
    rc->bitrate_or_rate = 2048000u; /* operator policy: 2048 decimal kbit/s */
    rc->i_min_qp = 30u; rc->i_max_qp = 50u;
    rc->p_min_qp = 30u; rc->p_max_qp = 50u;
    rc->i_proportion = 5u; rc->p_proportion = 1u;
    rc->fluctuate_level = 0u; rc->ip_qp_delta = 3;
    rc->still_rate_percent = 30u; rc->max_rate_percent = 120u;
    rc->max_still_qp = 38u;
}

static int encoder_rc_commit(struct app *a, const struct pae_rc_cfg *candidate)
{
    struct fh_h264_control control = {owner_h264_ioctl, a, 0};
    struct pae_rc_cfg old, next;
    uint32_t active;
    int rc, rollback_rc = 0, restart_rc;
    if (!a || !candidate) return -EINVAL;
    if (fh_pae_rc_validate_sdk_policy(candidate)) return -EINVAL;
    rc = encoder_rc_get(a, &old);
    if (rc) return rc;
    next = *candidate;
    active = a->venc_active_mask & 1u;
    if (active && (rc = venc_set_started(a, active, 0)) != 0) return rc;
    rc = fh_h264_set_rc_cold(&control, &next);
    if (rc) {
        rollback_rc = fh_h264_set_rc_cold(&control, &old);
    }
    restart_rc = active ? venc_set_started(a, active, 1) : 0;
    printf("ENCODER_RC commit_rc=%d rollback_rc=%d restart_rc=%d rate=%u mode=%u qp=%u I=%u..%u P=%u..%u\n",
           rc, rollback_rc, restart_rc, next.bitrate_or_rate, next.rc_mode,
           next.init_qp, next.i_min_qp, next.i_max_qp,
           next.p_min_qp, next.p_max_qp);
    if (rc) return rc;
    return restart_rc;
}

static void lens_status(struct app *a)
{
    uint32_t raw = 0;
    int target = fh8626_dualsensor_get(&a->dualsensor, &raw);
    FILE *state;

    state = fopen("/tmp/fh8626_lens.state.tmp", "w");
    if (state) {
        fprintf(state, "%s\n", fh8626_dualsensor_name(target));
        if (!fclose(state))
            (void)rename("/tmp/fh8626_lens.state.tmp",
                         "/tmp/fh8626_lens.state");
    }
    printf("LENS target=%d(%s) raw=%08x busy=%d venc_mask=%x ok=%u fail=%u rollback=%u gpio_sw=%u gpio_verify_fail=%u\n",
           target, fh8626_dualsensor_name(target), raw, a->lens_switch_busy,
           a->venc_active_mask, a->lens_switches_ok, a->lens_switches_fail,
           a->lens_rollbacks, a->dualsensor.switches,
           a->dualsensor.verify_failures);
    printf("LENS gpio_undo=%u undo_fail=%u last_undo_rc=%d\n",
           a->dualsensor.rollback_attempts,a->dualsensor.rollback_failures,
           a->dualsensor.last_rollback_error);
    printf("LENS last_rc=%d recovery_rc=%d control_running=%d\n",
           a->lens_last_error,a->lens_last_rollback_error,a->running);
}

static int lens_reselect(void *opaque, int target)
{
    struct app *a = opaque;
    return fh8626_dualsensor_select(&a->dualsensor,target,NULL,NULL);
}

static void lens_settle(void *opaque)
{
    (void)opaque;
    usleep(250000);
}

static int lens_switch(struct app *a, int target)
{
    uint32_t before = 0, after = 0;
    struct fh_lens_snapshot saved = {0};
    struct fh_lens_recovery_report recovery = {0};
    int old_target, was_running, rc, r2, recovery_rc;

    if (!a || !a->dualsensor.opened) return -ENODEV;
    if (target != FH8626_LENS_WIDE && target != FH8626_LENS_TELE)
        return -EINVAL;
    if (a->lens_switch_busy) return -EBUSY;
    old_target = fh8626_dualsensor_get(&a->dualsensor, NULL);
    if (old_target == target) {
        printf("LENS already=%s\n", fh8626_dualsensor_name(target));
        lens_status(a);
        return 0;
    }
    if (old_target != FH8626_LENS_WIDE && old_target != FH8626_LENS_TELE)
        return -EIO;

    rc = fh_lens_snapshot_read(&a->sensor,&saved);
    if (rc) {
        a->lens_last_error = rc;
        a->lens_switches_fail++;
        printf("LENS preflight rc=%d mux-unchanged=1\n",rc);
        return rc;
    }
    a->lens_last_error = a->lens_last_rollback_error = 0;
    a->lens_switch_busy = 1;
    was_running = a->running;
    set_running(a, 0);

    /* A mux write can have side effects even if its readback fails. Manual
     * AWB rollback must never cross this attempt, including rollback_gpio.
     * sidecar.generation alone is insufficient when streaming is disabled. */
    a->awb_mode1_saved.valid = 0;
    a->awb_mode1_saved_sensor_valid = 0;
    rc = fh8626_dualsensor_select(&a->dualsensor, target, &before, &after);
    printf("LENS gpio %s->%s before=%08x after=%08x rc=%d\n",
           fh8626_dualsensor_name(old_target), fh8626_dualsensor_name(target),
           before, after, rc);
    if (rc) goto rollback_gpio;

    /* PAE stop/start has historical repeat-safety failures. Keep the encoder
     * configured while ISP interrupt/control processing is held. This is not
     * a publication fence for already queued AUs (separate stage2 work). */
    usleep(250000);
    fh8626_sidecar_publisher_bump_generation(&a->sidecar);
    r2 = fh_sensor_gc1054_set_intt(&a->sensor, saved.intt);
    if (!rc && r2) rc = r2;
    r2 = fh_sensor_gc1054_set_gain(&a->sensor, saved.gain);
    if (!rc && r2) rc = r2;
    if (rc) goto rollback_gpio;

    a->warmup_left = 10;
    a->lens_switches_ok++;
    if (was_running) set_running(a, 1);
    a->lens_switch_busy = 0;
    printf("LENS switched=%s exposure=%u/%u sidecar_generation=%llu\n",
           fh8626_dualsensor_name(target), saved.intt, saved.gain,
           (unsigned long long)a->sidecar.generation);
    lens_status(a);
    return 0;

rollback_gpio:
    recovery_rc = fh_lens_restore(&a->sensor,old_target,&saved,
                                 lens_reselect,lens_settle,a,&recovery);
    a->lens_last_error = rc;
    a->lens_last_rollback_error = recovery_rc;
    a->lens_rollbacks++;
    a->lens_switches_fail++;
    /* Failed undo leaves AE/ISP interrupt handling on hold. This does not
     * claim an end-to-end stream fence: queued AU handling is stage2 work. */
    if (was_running && !recovery_rc) set_running(a, 1);
    a->lens_switch_busy = 0;
    printf("LENS switch failed target=%s rc=%d rollback=%u recovery_rc=%d select=%d intt=%d gain=%d readback=%d control-held=%d\n",
           fh8626_dualsensor_name(target), rc, a->lens_rollbacks,recovery_rc,
           recovery.select_error,recovery.intt_error,recovery.gain_error,
           recovery.readback_error,!a->running);
    lens_status(a);
    return rc;
}

static int set_named_profile(struct app *a, const char *name)
{
    struct fh_isp_runtime saved_rt = a->isp_rt;
    int rc;

    if (!a->nr3d_disabled) {
        rc=kernel_nr3d_set(a,0);
        if(rc){
            printf("ISP_PROFILE reject=%s nr3d_off_rc=%d profile-unchanged=1\n",name,rc);
            return rc;
        }
    }
    rc = load_named_profile(a, "/usr/share/fh8626/sensor_gc1054_mipi.bin",
                            name);
    if (rc) {
        a->isp_rt = saved_rt;
        if (!a->nr3d_disabled) {
            fh_isp_runtime_prepare_nr3d_reenable(&a->isp_rt);
            a->nr3d_reenable_pending=1;
        }
        printf("ISP_PROFILE reject=%s rc=%d rollback=1 active=%s\n",
               name, rc, a->isp_profile);
        return rc;
    }
    snprintf(a->isp_profile, sizeof(a->isp_profile), "%s", name);
    if (!a->nr3d_disabled) {
        fh_isp_runtime_prepare_nr3d_reenable(&a->isp_rt);
        a->nr3d_reenable_pending = 1;
    }
    iq_capture_stock(a);
    iq_apply(a);
    printf("ISP_PROFILE staged=%s publication=next-runtime-tick\n",
           a->isp_profile);
    return 0;
}

static int board_light_command(const char *group, const char *value)
{
    const char *helper=getenv("FH8626_LIGHT_HELPER");
    pid_t child,waited;
    int status;
    if (!group || !value) return -EINVAL;
    if (!helper||!*helper) helper="/usr/sbin/fh-anjia-ajl33pq0866-light";
    if (helper[0]!='/') return -EINVAL;
    child=fork();
    if(child<0)return -errno;
    if(!child){execl(helper,helper,group,value,(char *)NULL);_exit(127);}
    do{waited=waitpid(child,&status,0);}while(waited<0&&errno==EINTR);
    if(waited<0)return -errno;
    return WIFEXITED(status)&&WEXITSTATUS(status)==0?0:-EIO;
}

static void illumination_publish_state(struct app *a)
{
    FILE *state = fopen("/tmp/fh8626_illum.state.tmp", "w");
    if (!state) return;
    fprintf(state, "scene=%s ircut=%s irled=%d white=%d light=%d\n",
            a->scene, a->ircut_state, a->ir_led_state,
            a->white_led_state, a->light_sensor_value);
    if (!fclose(state))
        (void)rename("/tmp/fh8626_illum.state.tmp",
                     "/tmp/fh8626_illum.state");
}

static void illumination_status(struct app *a)
{
    illumination_publish_state(a);
    printf("ILLUM scene=%s profile=%s ircut=%s irled=%d white=%d light=%d gpio_ir=25 gpio_white=23 gpio_ircut=18/60 sadc=1\n",
           a->scene, a->isp_profile, a->ircut_state, a->ir_led_state,
           a->white_led_state, a->light_sensor_value);
}

static int light_sensor_read(struct app *a, int verbose)
{
    FILE *pipe;
    char line[160];
    int channel, value, found = 0;

    if(a->white_led_state!=0)return -EBUSY; /* shared pad not established */

    /* pad70 is kept in SADC_CHANL1 whenever white light is off.  Reading the
     * driver directly avoids the old helper's GPIO/pinmux writes and 100-ms
     * sleep on every automatic-scene sample. */
    pipe = fopen("/proc/driver/sadc", "r");
    if (!pipe) return -errno;
    while (fgets(line, sizeof(line), pipe)) {
        if (sscanf(line, "channel: %d value: %d", &channel, &value) == 2 &&
            channel == 1) {
            if(value<0){fclose(pipe);return -ENODATA;}
            a->light_sensor_value = value;
            found = 1;
        }
    }
    if (fclose(pipe) != 0 || !found) return -EIO;
    illumination_publish_state(a);
    if(verbose)
        printf("LIGHT_SENSOR channel=1 value=%d darker=lower white_forced_off=0 pad70=expected_sadc\n",
               a->light_sensor_value);
    return 0;
}

static uint64_t monotonic_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts)) return 0;
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static int scene_mode(const char *name)
{
    if (!strcmp(name,"day")) return FH_SCENE_DAY;
    if (!strcmp(name,"night")) return FH_SCENE_NIGHT;
    if (!strcmp(name,"wlight")) return FH_SCENE_WLIGHT;
    return -1;
}

static const char *scene_name(int mode)
{
    return mode==FH_SCENE_DAY?"day":mode==FH_SCENE_NIGHT?"night":
           mode==FH_SCENE_WLIGHT?"wlight":"unknown";
}

static int scene_apply(void *opaque,enum fh_scene_field field,int value)
{
    struct app *a=opaque;
    switch(field) {
    case FH_SCENE_IR: return board_light_command("ir",value?"on":"off");
    case FH_SCENE_WHITE: return board_light_command("white",value?"on":"off");
    case FH_SCENE_IRCUT: return board_light_command("ircut",value?"night":"day");
    case FH_SCENE_PROFILE: return set_named_profile(a,scene_name(value));
    }
    return -EINVAL;
}

static int scene_switch(struct app *a, const char *scene)
{
    struct fh_scene_state state={{a->ir_led_state,a->white_led_state,
        scene_mode(a->ircut_state),scene_mode(a->isp_profile)}};
    struct fh_scene_report report={0};
    int target=scene_mode(scene),rc;
    if(target<0)return -EINVAL;
    rc=fh_scene_transition(&state,target,scene_apply,a,&report);
    a->ir_led_state=state.value[FH_SCENE_IR];
    a->white_led_state=state.value[FH_SCENE_WHITE];
    snprintf(a->ircut_state,sizeof(a->ircut_state),"%s",scene_name(state.value[FH_SCENE_IRCUT]));
    snprintf(a->isp_profile,sizeof(a->isp_profile),"%s",scene_name(state.value[FH_SCENE_PROFILE]));
    if(!rc)snprintf(a->scene,sizeof(a->scene),"%s",scene);
    else if(report.rollback_error)snprintf(a->scene,sizeof(a->scene),"unknown");
    illumination_publish_state(a);
    printf("SCENE target=%s rc=%d failed_step=%u undo=%u rollback_rc=%d active=%s profile=%s ircut=%s irled=%d white=%d\n",
           scene,rc,report.failed_step,report.undo_attempts,report.rollback_error,
           a->scene,a->isp_profile,a->ircut_state,a->ir_led_state,a->white_led_state);
    return rc;
}
static int scene_auto_step(struct app *a, int force)
{
    uint32_t night_threshold, day_threshold;
    const char *target = NULL;
    uint64_t now;
    uint32_t sample, metric;
    const char *metric_source;
    int is_night, rc;

    if (!a || !a->scene_auto_enabled) return 0;
    now = monotonic_ms();
    if (!force && now && now < a->scene_auto_next_ms) return 0;
    a->scene_auto_next_ms = now + 200u;

    /* Auto mode must establish shared-pad SADC ownership when entering from
     * white/manual/unknown state, not interpret a disconnected ADC sample. */
    if(a->white_led_state!=0){
        rc=board_light_command("white","off");
        a->white_led_state=rc?-1:0;
        if(rc){a->scene_auto_next_ms=now+2000u;return rc;}
    }

    /* Day/night is board illumination policy and uses the board SADC1 scale.
     * DE6CC returns a stock level selected through its own gain-normalized
     * history/table policy; treating its intermediate sample as an SADC value
     * caused false night transitions (live sample 8 in a brightly lit room). */
    if (light_sensor_read(a, 0)) return -EAGAIN;
    sample = (uint32_t)a->light_sensor_value;
    metric_source = "sadc1";
    night_threshold = 300u;
    day_threshold = 360u;
    if (!a->scene_metric_count) {
        unsigned i;
        for (i = 0; i < 10u; ++i) a->scene_metric_history[i] = sample;
        a->scene_metric_sum = sample * 10u;
        a->scene_metric_count = 10u;
        a->scene_metric_index = 0u;
    } else {
        a->scene_metric_sum -= a->scene_metric_history[a->scene_metric_index];
        a->scene_metric_history[a->scene_metric_index] = sample;
        a->scene_metric_sum += sample;
        a->scene_metric_index = (a->scene_metric_index + 1u) % 10u;
    }
    metric = a->scene_metric_sum / 10u;

    if (!strcmp(a->ircut_state, "night")) is_night = 1;
    else if (!strcmp(a->ircut_state, "day")) is_night = 0;
    else is_night = metric < night_threshold;

    if (is_night && metric > day_threshold) target = "day";
    else if (!is_night && metric < night_threshold) target = "night";
    else if (strcmp(a->ircut_state, "day") && strcmp(a->ircut_state, "night"))
        target = is_night ? "night" : "day";

    /* D8128 stock daynight latency is 2000 ms. */
    if (target && a->scene_auto_last_switch_ms && now &&
        now - a->scene_auto_last_switch_ms < 2000u)
        target = NULL;
    if (target) {
        rc = scene_switch(a, target);
        if (rc) { a->scene_auto_next_ms=now+2000u; return rc; }
        a->scene_auto_last_switch_ms = now;
    }
    snprintf(a->scene, sizeof(a->scene), "auto");
    illumination_publish_state(a);
    if(force || target || now >= a->scene_log_next_ms) {
        a->scene_log_next_ms=now+30000u;
        printf("SCENE_AUTO metric=%u sample=%u history=10 thresholds=%u/%u effective=%s changed=%d source=%s\n",
               metric, sample, night_threshold, day_threshold,
               a->ircut_state, target != NULL, metric_source);
    }
    return 0;
}

static int scene_auto_enable(struct app *a)
{
    if (!a) return -EINVAL;
    a->scene_auto_enabled = 1;
    a->scene_auto_next_ms = 0;
    a->scene_metric_sum = 0u;
    a->scene_metric_index = 0u;
    a->scene_metric_count = 0u;
    snprintf(a->scene, sizeof(a->scene), "auto");
    illumination_publish_state(a);
    return scene_auto_step(a, 1);
}

static void meterprobe(struct app *a)
{
    /* C67C4(0) was observed at imem+0x1487b8 and returns root+0x48.
     * Therefore imem+0x148770 is the first evidence-derived isp_cfg root;
     * retain ioctl buffer candidates as negative controls. */
    static const uint32_t off[] = {0x148770u, 0u, 0x119400u, 0x127500u};
    volatile uint8_t *saved_base = a->isp_rt.isp_cfg;
    size_t saved_size = a->isp_rt.isp_cfg_size;
    uint32_t saved_sum[9], saved_count[9];
    unsigned candidate, i;
    memcpy(saved_sum, a->isp_rt.ltm_group_sum, sizeof(saved_sum));
    memcpy(saved_count, a->isp_rt.ltm_group_count, sizeof(saved_count));
    for (candidate = 0u; candidate < sizeof(off) / sizeof(off[0]); ++candidate) {
        uint32_t sample = 0u;
        int rc;
        if (off[candidate] >= a->imem.size) continue;
        rc = fh_isp_runtime_attach_isp_cfg(&a->isp_rt,
                (volatile uint8_t *)(uintptr_t)a->imem.virt + off[candidate],
                a->imem.size - off[candidate]);
        if (!rc) rc = fh_isp_runtime_snapshot_c6c00(&a->isp_rt);
        if (!rc) rc = fh_isp_runtime_de6cc_scene_sample(&a->isp_rt, &sample);
        printf("METERPROBE base=+0x%x rc=%d sample=%u sums=", off[candidate], rc, sample);
        for (i = 0u; i < 9u; ++i) printf("%s%u", i ? "," : "", a->isp_rt.ltm_group_sum[i]);
        printf(" counts=");
        for (i = 0u; i < 9u; ++i) printf("%s%u", i ? "," : "", a->isp_rt.ltm_group_count[i]);
        printf("\n");
    }
    a->isp_rt.isp_cfg = saved_base;
    a->isp_rt.isp_cfg_size = saved_size;
    memcpy(a->isp_rt.ltm_group_sum, saved_sum, sizeof(saved_sum));
    memcpy(a->isp_rt.ltm_group_count, saved_count, sizeof(saved_count));
}

static void status(struct app *a)
{
    uint32_t st[12];
    memset(st, 0, sizeof(st));
    int ret = ioctl(a->pae, PAE_GET_STATUS, st);
    printf("STATE running=%d capture=%u warmup=%u probe=%u drained=%llu 024=%08x 028=%08x 030=%08x 078=%08x 080=%08x 008=%08x 004=%08x 5c4=%08x\n",
           a->running, a->cap_left, a->warmup_left, a->probe_left, (unsigned long long)a->drain_frames, rr(a->r,0x024), rr(a->r,0x028), rr(a->r,0x030), rr(a->r,0x078), rr(a->r,0x080), rr(a->r,0x008), rr(a->r,0x004), rr(a->r,0x5c4));
    printf("ALGO path=%s loaded=%d\n", a->algo_path[0] ? a->algo_path : "(none)", a->algo_tick != NULL);
    printf("SIDECAR enabled=%d path=%s generation=%llu queued=%llu dropped=%llu clients=%llu failures=%llu non_annexb=%llu prefix_bytes=%llu\n",
           a->sidecar.initialized,
           a->sidecar.initialized ? a->sidecar.socket_path : "(disabled)",
           (unsigned long long)a->sidecar.generation,
           (unsigned long long)a->sidecar.frames_queued,
           (unsigned long long)a->sidecar.frames_dropped,
           (unsigned long long)a->sidecar.clients_accepted,
           (unsigned long long)a->sidecar_publish_failures,
           (unsigned long long)a->sidecar_non_annexb,
           (unsigned long long)a->sidecar_prefix_bytes);
    printf("AE mode=%s target=%u stock_target=%u mean=%u updates=%llu failures=%llu\n",
           a->ae_auto_enabled ? "auto" : "manual", a->ae_target,
           a->ae_stock_target, a->ae_last_mean,
           (unsigned long long)a->ae_updates,
           (unsigned long long)a->ae_failures);
    printf("ANTIFLICKER mode=%s hz=%u quantum_lines=%u stock_selector=%u\n",
           !a->antiflicker_hz ? "off" :
           (a->antiflicker_hz == 1u ? "stock" :
            (a->antiflicker_hz == 50u ? "50hz" : "60hz")),
           a->antiflicker_hz > 1u ? a->antiflicker_hz : 0u,
           antiflicker_quantum_lines(a), a->isp_rt.ctx[0xa4c]);
    printf("PAE_STATUS ret=%d", ret);
    for (int i = 0; i < 12; i++) printf(" %08x", st[i]);
    printf("\n");
    lens_status(a);
    illumination_status(a);
}

static void setreg(struct app *a, unsigned off, uint32_t val)
{
    if ((off & 3U) || off >= FH_ISP_MMIO_SIZE) { printf("SETREG rejected off=%x\n", off); return; }
    int was_running = a->running;
    wr(a->r, 0x008, 0);
    __sync_synchronize();
    uint32_t old = rr(a->r, off);
    wr(a->r, off, val);
    __sync_synchronize();
    printf("SETREG +%03x %08x -> %08x readback=%08x\n", off, old, val, rr(a->r, off));
    if (was_running) { a->warmup_left = 5; wr(a->r, 0x008, ISP_MASK); }
}

static void dump_state(struct app *a, const char *tag)
{
    char p[256];
    int was_running = a->running;
    if (!tag || !*tag) tag = "fh";
    wr(a->r, 0x008, 0);
    __sync_synchronize();
    snprintf(p, sizeof(p), "/tmp/%s_isp_vmm.bin", tag);
    dump_binary(p, (const void *)(uintptr_t)a->imem.virt, a->imem.size);
    snprintf(p, sizeof(p), "/tmp/%s_isp_mmio.bin", tag);
    dump_binary(p, (const void *)(uintptr_t)a->r, FH_ISP_MMIO_SIZE);
    if (was_running) { a->warmup_left = 5; wr(a->r, 0x008, ISP_MASK); }
}

static const uint8_t *find_awb_stats(struct app *a, uint32_t *off_out)
{
    const uint32_t off = 0x1487b8u;
    /* CA4F4 owns per-cell validity and fallback.  Rejecting the complete
     * block because one of nine cells is clipped changes stock semantics. */
    if (!a || !a->e2_stats_valid)
        return NULL;
    if (off_out) *off_out = off;
    return a->e2_stats;
}

static int ae_auto_step(struct app *a)
{
    uint32_t intt = 0, gain = 0, mean, next;
    uint32_t imax = ae_effective_intt_max(a);
    uint32_t gmax = app_ctx_u16(a->isp_rt.ctx + 0x36);
    uint32_t gsoft = app_ctx_u16(a->isp_rt.ctx + 0x40);
    uint32_t enter_delta, leave_delta, dwell, abs_delta;
    float factor;
    int rc = 0;
    if (gsoft > gmax) gsoft = gmax;
    if (gsoft < 64u) gsoft = 64u;
    if (a->antiflicker_hz) {
        uint32_t q = antiflicker_quantum_lines(a);
        if (q > 1u && imax >= q) imax = (imax / q) * q;
    }
    if (!a->ae_auto_enabled || !a->e2_ae_pending) return 0;
    a->e2_ae_pending = 0;
    /* Sensor integration/gain affects later frames.  With anti-flicker active,
     * leave three statistic epochs for the previous actuator write to settle;
     * otherwise the delayed feedback oscillates between adjacent quanta. */
    if (a->antiflicker_hz && (++a->ae_settle_epoch & 3u)) return 0;
    /* Stock C949C uses C6C00's LTM group snapshot and C757C's selected
     * 3x3 weighting mode. The former E2 green/count average ignored bright
     * zones and allowed clipped white regions to acquire a green cast. */
    mean = a->isp_rt.c757c_metric_q12;
    if (!mean) { a->ae_failures++; return -ENODATA; }
    a->ae_last_mean = mean;
    /* C757C saturates the full state metric at 4096; that endpoint is a
     * valid bright measurement even though ctx+0x58 packs only 12 bits. */
    if (mean > 4096u) { a->ae_failures++; return -ERANGE; }
    /* C7C3C is a two-threshold state machine.  Profile bytes +0x31/+0x32
     * are Q12 deltas in units of 16 and +0x33 is the consecutive-epoch
     * dwell.  Preserve that hysteresis before entering/leaving C883C-style
     * actuation; a percentage deadband is not stock-equivalent. */
    leave_delta = (uint32_t)a->isp_rt.ctx[0x31] * 16u;
    enter_delta = (uint32_t)a->isp_rt.ctx[0x32] * 16u;
    dwell = a->isp_rt.ctx[0x33];
    if (!leave_delta) leave_delta = 1u;
    if (enter_delta < leave_delta) enter_delta = leave_delta;
    if (!dwell) dwell = 1u;
    abs_delta = mean > a->ae_target ? mean - a->ae_target : a->ae_target - mean;
    if (!a->ae_control_active) {
        if (abs_delta >= enter_delta) {
            if (++a->ae_state_count >= dwell) {
                a->ae_control_active = 1;
                a->ae_state_count = 0;
            }
        } else {
            a->ae_state_count = 0;
        }
    } else if (abs_delta < leave_delta) {
        if (++a->ae_state_count > dwell) {
            a->ae_control_active = 0;
            a->ae_state_count = 0;
        }
    } else {
        a->ae_state_count = 0;
    }
    rc = fh_isp_runtime_publish_control_metric(&a->isp_rt, a->e2_stats_epoch,
                                                (int32_t)mean,
                                                (int32_t)a->ae_target);
    if (rc) { a->ae_failures++; return rc; }
    a->ae_state_epoch = a->e2_stats_epoch;
    if (!a->ae_control_active) return 0;
    rc = fh_sensor_gc1054_get_intt(&a->sensor, &intt);
    if (!rc) rc = fh_sensor_gc1054_get_gain(&a->sensor, &gain);
    if (rc) { a->ae_failures++; return rc; }
    factor = ae_c7eb0_factor(mean, a->ae_target, a->isp_rt.ctx[0x3c]);
    next = intt;
    if (mean < a->ae_target) {
        /* C883C dark priority for shipped flags 0x03: direct integration to
         * ctx+0x38, then sensor gain to ctx+0x40 and finally ctx+0x36. */
        if (intt < imax) {
            next = (uint32_t)((float)intt * factor);
            if (next <= intt) next = intt + 1u;
            if (next > imax) next = imax;
            next = antiflicker_quantize(a, next, imax, 1);
            rc = fh_sensor_gc1054_set_intt(&a->sensor, next);
        } else if (gain < gsoft) {
            next = (uint32_t)((float)gain * factor);
            if (next <= gain) next = gain + 1u;
            if (next > gsoft) next = gsoft;
            rc = fh_sensor_gc1054_set_gain(&a->sensor, next);
        } else if (gain < gmax) {
            next = (uint32_t)((float)gain * factor);
            if (next <= gain) next = gain + 1u;
            if (next > gmax) next = gmax;
            rc = fh_sensor_gc1054_set_gain(&a->sensor, next);
        }
    } else if (mean > a->ae_target) {
        /* C883C drains sensor gain/Q8 stages before final direct integration. */
        if (gain > 64u) {
            next = (uint32_t)((float)gain * factor);
            if (next >= gain) next = gain - 1u;
            if (next < 64u) next = 64u;
            rc = fh_sensor_gc1054_set_gain(&a->sensor, next);
        } else if (intt > 2u) {
            next = (uint32_t)((float)intt * factor);
            if (next >= intt) next = intt - 1u;
            if (next < 2u) next = 2u;
            next = antiflicker_quantize(a, next, imax, 0);
            rc = fh_sensor_gc1054_set_intt(&a->sensor, next);
        }
    }
    if (rc) a->ae_failures++; else a->ae_updates++;
    if (a->ae_updates <= 12u || !(a->ae_updates % 25u))
        printf("AE_AUTO mean=%u target=%u sensor=%u/%u next=%u gain_soft=%u antiflicker_q=%u rc=%d updates=%llu\n",
               mean, a->ae_target, intt, gain, next,
               gsoft,
               antiflicker_quantum_lines(a), rc,
               (unsigned long long)a->ae_updates);
    return rc;
}

static int ae_stock_runtime_step(struct app *a)
{
    uint32_t mean, target;
    int rc;
    if (!a->ae_auto_enabled || !a->e2_ae_pending) return 0;
    a->e2_ae_pending = 0;
    mean = a->isp_rt.c757c_metric_q12;
    /* Zero luminance is valid after C73F8 population validation upstream. */
    a->ae_last_mean = mean;
    if (mean > 4096u) { a->ae_failures++; return -ERANGE; }
    if (!a->ae_rt.commit_enabled) {
        if ((int8_t)a->isp_rt.ctx[0x2f] >= 0) return -ENODATA;
        a->e2_ae_pending = 1;
        return ae_auto_step(a);
    }
    /* C6D04 history has one owner.  The stock AE runtime below implements
     * C6D04 immediately before C90D4; do not maintain a second ISP-runtime
     * copy for the same epoch. */
    target = a->ae_target == a->ae_stock_target
        ? a->isp_rt.control_target_q12 : a->ae_target;
    rc = fh8626_ae_runtime_step_metric(&a->ae_rt, mean, target);
    /* A missing stock provider must not trigger a second, unrelated actuator
     * pass after the stock queue has already been committed this epoch. */
    if (rc) a->ae_failures++;
    else a->ae_updates = a->ae_rt.updates;
    if (a->control_frames <= 12u || !(a->control_frames % 25u))
        printf("AE_STOCK mean=%u target=%u action=%u factor_q12=%u intt=%u gain=%u commits=%u rc=%d\n",
               mean, target, a->ae_rt.action_code,
               a->ae_rt.last_factor_q12, a->ae_rt.current_intt,
               a->ae_rt.current_gain, a->ae_rt.commits, rc);
    return rc;
}

static void awb_mode1_commit_init_from_regs(struct fh_stock_awb_commit_state *st,
                                            uint32_t isp024, uint32_t isp084,
                                            uint32_t isp088, uint32_t r224,
                                            uint32_t r228)
{
    fh_stock_awb_commit_seed_bayer(st,isp024,isp084,isp088,r224,r228);
}

static int awb_mode1_save(struct app *a)
{
    if (a->awb_mode1_saved.valid &&
        a->awb_mode1_saved.profile_generation==a->isp_rt.profile_generation &&
        a->awb_mode1_saved.stream_generation==a->sidecar.generation) return 0;
    a->awb_mode1_saved.valid=0;
    a->awb_mode1_saved_sensor_valid = 0;
    /* Manual rollback is not a stock periodic operation. Do not promise it
     * can undo a write-only optional sensor callback without readback. */
    if ((a->isp_rt.ctx[0x6e] & 2u) &&
        fh_sensor_gc1054_cb(&a->sensor,0x5c)) {
        if (!fh_sensor_gc1054_cb(&a->sensor,0x58)) return -EOPNOTSUPP;
        memset(a->awb_mode1_saved_sensor_gain,0,sizeof(a->awb_mode1_saved_sensor_gain));
        fh_sensor_gc1054_awb_query(&a->sensor,a->awb_mode1_saved_sensor_gain);
        if (!a->awb_mode1_saved_sensor_gain[0] || !a->awb_mode1_saved_sensor_gain[1] ||
            !a->awb_mode1_saved_sensor_gain[2]) return -EDOM;
        a->awb_mode1_saved_sensor_valid = 1;
    }
    fh_stock_awb_snapshot_save(&a->awb_mode1_saved,&a->awb_mode0,a->isp_rt.ctx,
        a->r,a->isp_rt.profile_generation,a->sidecar.generation);
    return 0;
}

static void awb_mode1_step(struct app *a)
{
    struct fh_stock_ca4f4_diag d;
    uint8_t mode = a->isp_rt.ctx[0x6c];
    int paused = a->awb_mode0.mode1_paused, rc;
    if (a->awb_mode0_enabled) {
        a->awb_last_rc=-EBUSY;
        printf("AWBMODE1STEP mode0 trial active; restore it first\n");
        return;
    }
    a->awb_mode0.sensor_gain = fh_sensor_gc1054_awb_gain;
    a->awb_mode0.sensor_query = fh_sensor_gc1054_awb_query;
    a->awb_mode0.sensor_gain_opaque = &a->sensor;
    rc = awb_mode1_save(a);
    if (rc) {
        a->awb_last_rc = rc;
        printf("AWBMODE1STEP snapshot unavailable rc=%d; no AWB commit\n",rc);
        return;
    }
    memset(&d,0,sizeof(d));
    /* Explicit diagnostic command selects mode1 for this call only. */
    a->isp_rt.ctx[0x6c] = (mode & (uint8_t)~3u) | 1u;
    a->awb_mode0.mode1_paused = 0;
    rc = fh_stock_awb_dispatch(&a->awb_mode0,a->isp_rt.ctx,a->r,
                                a->e2_stats_valid ? a->e2_stats : NULL,&d);
    a->isp_rt.ctx[0x6c] = mode;
    a->awb_mode0.mode1_paused = paused;
    if (!rc) rc = fh_isp_runtime_apply_ce764(&a->isp_rt);
    a->awb_last_rc = rc;
    __sync_synchronize();
    printf("AWBMODE1STEP rc=%d estimator=%d valid=%u statsfb=%d gatefb=%d recovery=%u/%u\n",
           rc,a->awb_mode0.last_estimator_rc,d.valid_count,d.stats_fallback,d.gate_fallback,
           a->awb_mode0.mode1.recovery_count,a->awb_mode0.mode1.recovery_active);
}

static void awb_mode1_restore(struct app *a)
{
    uint32_t readback[3], target[3];
    int rc;
    if (!a->awb_mode1_saved.valid) {
        printf("AWBMODE1RESTORE nothing saved (automatic ticks are not manual trials)\n");
        return;
    }
    if (a->awb_mode1_saved.profile_generation != a->isp_rt.profile_generation ||
        a->awb_mode1_saved.stream_generation != a->sidecar.generation) {
        a->awb_mode1_saved.valid = 0;
        printf("AWBMODE1RESTORE stale profile/lens epoch; no writes\n");
        return;
    }
    if (a->awb_mode1_saved_sensor_valid) {
        if (!fh_sensor_gc1054_cb(&a->sensor,0x58) || !fh_sensor_gc1054_cb(&a->sensor,0x5c)) {
            printf("AWBMODE1RESTORE sensor provider unavailable; no local restore\n");
            return;
        }
        memcpy(target,a->awb_mode1_saved_sensor_gain,sizeof(target));
        fh_sensor_gc1054_awb_gain(&a->sensor,target);
        memset(readback,0,sizeof(readback));
        fh_sensor_gc1054_awb_query(&a->sensor,readback);
        if (memcmp(readback,a->awb_mode1_saved_sensor_gain,sizeof(readback))) {
            printf("AWBMODE1RESTORE sensor readback mismatch; local snapshot retained\n");
            return;
        }
    }
    rc = fh_stock_awb_snapshot_restore(&a->awb_mode1_saved,&a->awb_mode0,
        a->isp_rt.ctx,a->r,a->isp_rt.profile_generation,a->sidecar.generation);
    __sync_synchronize();
    printf("AWBMODE1RESTORE rc=%d 224=%08x 228=%08x 4bc=%08x ctx_ac=%08x\n",
           rc,rr(a->r,0x224),rr(a->r,0x228),rr(a->r,0x4bc),app_ctx_u32(a->isp_rt.ctx+0xac));
}

static void awb_mode1_status(struct app *a)
{
    printf("AWBMODE1STATUS saved=%d 224=%08x 228=%08x 4bc=%08x logical=%d,%d,%d epoch=%u recovery=%u/%u\n",
           a->awb_mode1_saved.valid,rr(a->r,0x224),rr(a->r,0x228),rr(a->r,0x4bc),
           a->awb_mode0.commit.cur0,a->awb_mode0.commit.cur1,a->awb_mode0.commit.cur2,
           a->awb_mode0.dispatch_epoch,a->awb_mode0.mode1.recovery_count,
           a->awb_mode0.mode1.recovery_active);
}

static void awb_mode1_diag(struct app *a)
{
    struct fh_stock_ca4f4_diag d;
    struct fh_stock_awb_commit_state fallback_state;
    uint8_t ctx_first[0xa5c];
    uint8_t normalized[9*16];
    uint32_t ioctl_off = 0, stats_off = 0;
    const uint8_t *stats;
    int irc, rc;
    unsigned i;
    errno = 0;
    irc = ioctl(a->isp, ISP_6905, &ioctl_off);
    stats = find_awb_stats(a, &stats_off);
    printf("AWBMODE1MAP ioctl6905 ret=%d errno=%d off=0x%08x scan=%s stats_off=0x%08x bf4_off=0x%08x\n",
           irc, errno, ioctl_off, stats ? "FOUND" : "MISS", stats_off, stats ? stats_off - 0x48u : 0u);
    if (!stats) { printf("AWBMODE1MAP READONLY abort: real 9x16 stats block not found\n"); return; }
    memcpy(ctx_first, a->isp_rt.ctx, sizeof(ctx_first));
    ctx_first[0x71] |= 2u;
    if (a->awb_mode0.commit_valid) fallback_state = a->awb_mode0.commit;
    else awb_mode1_commit_init_from_regs(&fallback_state, rr(a->r,0x024),
                                         rr(a->r,0x084), rr(a->r,0x088),
                                         rr(a->r,0x224), rr(a->r,0x228));
    /* Read-only diagnostic uses a copy of cached normalization references;
       it must not invoke the optional sensor callback or alter shared state. */
    rc = fh_stock_awb_prepare_stats(&fallback_state,ctx_first,stats,normalized,NULL,NULL);
    if (rc) { printf("AWBMODE1DIAG READONLY unavailable normalization rc=%d\n",rc); return; }
    {
        const int16_t last_good[3] = {fallback_state.cur0, fallback_state.cur1,
                                      fallback_state.cur2};
        rc = fh_stock_ca4f4_diag_compute(&d, ctx_first, a->r, normalized, last_good);
    }
    printf("AWBMODE1DIAG_FIRST rc=%d valid=%u countsum=%u min02=%u max02=%u spread=%u sum02=%u sum21=%u sum01=%u\n",
           rc,d.valid_count,d.count_sum,d.ratio_min,d.ratio_max,d.ratio_max-d.ratio_min,d.ratio_sum_02,d.ratio_sum_21,d.ratio_sum_01);
    printf("AWBMODE1DIAG_FIRST ctx71_live=%02x weight=%u median=%u statsfb=%d gate_bypassed_temporal=1\n",
           a->isp_rt.ctx[0x71],d.weight,d.median_slot,d.stats_fallback);
    printf("AWBMODE1DIAG_FIRST fallback=%u,%u,%u base=%d,%d,%d robust=%d,%d,%d mixed=%d,%d,%d final=%d,%d,%d 4bc_cur=%08x 4bc_calc=%08x\n",
           d.fallback[0],d.fallback[1],d.fallback[2],d.base[0],d.base[1],d.base[2],d.robust[0],d.robust[1],d.robust[2],
           d.mixed[0],d.mixed[1],d.mixed[2],d.final_gain[0],d.final_gain[1],d.final_gain[2],rr(a->r,0x4bc),d.reg4bc_next);
    for(i=0;i<9;i++) printf("AWBMODE1STAT i=%u c0=%u c1=%u c2=%u n=%u q02=%u\n",i,
        d.raw[i][0],d.raw[i][1],d.raw[i][2],d.raw[i][3],d.ratio_c0_c2[i]);
    printf("AWBMODE1DIAG READONLY no ISP/AWB writes performed\n");
}

static void help(void)
{
    printf("COMMANDS via %s:\n", CTL_PATH);
    printf("  status\n  run\n  stop\n  get OFFHEX\n  set OFFHEX VALHEX\n");
    printf("  probe [frames]\n  capture [frames] [path] [max_kib]\n  sensor status|set INTT GAIN\n  lens status|wide|tele\n  scene status|auto|day|night|wlight\n  ircut status|day|night\n  irled status|on|off\n  white status|on|off\n  lightsensor status|read\n  ae status|auto|off|target VALUE|reset-stock\n  antiflicker status|off|50|60|stock\n  profile status|day|night|wlight\n  iq status|brightness|contrast|saturation|sharpness 0..255|reset\n  encoder status|bitrate BPS|rcmode 0..5|refresh|qp INIT IMIN IMAX PMIN PMAX\n  dump [tag]\n  reload [algo.so]\n  unloadalgo\n  awbmode0 on|off|status\n  awbmode1 diag|step|restore|status\n  shutdown\n  base\n  help\n");
    printf("SIGINT/SIGTERM => safe hold, process stays owner\n");
}

static int owner_nr3d_ioctl(void *opaque,unsigned long request,void *arg)
{
    struct app *a=opaque;
    int rc;
    if(!a||a->isp<0)return -EBADF;
    errno=0;
    rc=ioctl(a->isp,request,arg);
    return rc==0?0:-(errno?errno:EIO);
}

static int kernel_nr3d_set(struct app *a,int enabled)
{
    struct fh_nr3d_kernel kernel={owner_nr3d_ioctl,a,"/proc/driver/isp"};
    /* The legacy software warmup is not a proven hardware history reset.
     * Keep ON unavailable until a cold/restart lifecycle adapter is wired. */
    if(enabled)return -EOPNOTSUPP;
    return fh_nr3d_kernel_set_verified(&kernel,0,NULL);
}

static int nr3d_query_config(void *opaque,struct fh_nr3d_driver_config *out)
{
    struct app *a=opaque;
    return ioctl(a->isp,FH_ISP_NR3D_QUERY,out)==0?0:-(errno?errno:EIO);
}

static void command(struct app *a, char *line);

static int kernel_nr3d_request(void *opaque,int enabled)
{
    return kernel_nr3d_set(opaque,enabled);
}

static int nr3d_request(struct app *a,int enabled)
{
    /* Reject before selector/MMIO/software-state mutation, not after warmup. */
    if(enabled){
        fprintf(stderr,"NR3D enable requires verified cold/restart integration; unchanged\n");
        return -EOPNOTSUPP;
    }
    uint32_t saved=rr(a->r,0x468);
    int was_disabled=a->nr3d_disabled;
    int rc=fh_nr3d_request(&a->isp_rt,&a->nr3d_disabled,&a->nr3d_reenable_pending,
                           enabled,kernel_nr3d_request,a);
    if(!rc&&!was_disabled&&!enabled)a->nr3d_saved_468=saved;
    return rc;
}

static void command_enqueue(struct app *a, const char *line)
{
	size_t len;

    if (a->cmd_count == CMD_QUEUE_CAP) {
        a->cmd_dropped++;
        printf("CONTROL QUEUE FULL dropped=%u\n", a->cmd_dropped);
        return;
    }
	len = strnlen(line, CMD_LINE_CAP - 1);
	memcpy(a->cmd_queue[a->cmd_tail], line, len);
	a->cmd_queue[a->cmd_tail][len] = '\0';
    a->cmd_tail = (a->cmd_tail + 1) % CMD_QUEUE_CAP;
    a->cmd_count++;
}

static void command_drain(struct app *a)
{
    unsigned budget = 4;
    while (a->cmd_count && budget--) {
        char line[CMD_LINE_CAP];
        snprintf(line, sizeof(line), "%s", a->cmd_queue[a->cmd_head]);
        a->cmd_head = (a->cmd_head + 1) % CMD_QUEUE_CAP;
        a->cmd_count--;
        command(a, line);
    }
}

static void command(struct app *a, char *line)
{
    char *argv[8] = {0};
    int argc = 0;
    char *save = NULL;
    for (char *t = strtok_r(line, " \t\r\n", &save); t && argc < 8; t = strtok_r(NULL, " \t\r\n", &save)) argv[argc++] = t;
    if (!argc) return;
    if (!strcmp(argv[0], "status")) status(a);
    else if (!strcmp(argv[0], "run")) set_running(a, 1);
    else if (!strcmp(argv[0], "stop")) set_running(a, 0);
    else if (!strcmp(argv[0], "shutdown")) {
        printf("SHUTDOWN requested; owner will teardown and exit\n");
        a->shutdown_requested = 1;
    }
    else if (!strcmp(argv[0], "help")) help();
    else if (!strcmp(argv[0], "meterprobe")) meterprobe(a);
    else if (!strcmp(argv[0], "get") && argc >= 2) { unsigned off = strtoul(argv[1], NULL, 16); printf("GETREG +%03x = %08x\n", off, rr(a->r, off)); }
    else if (!strcmp(argv[0], "set") && argc >= 3) setreg(a, strtoul(argv[1], NULL, 16), strtoul(argv[2], NULL, 16));
    else if (!strcmp(argv[0], "probe")) {
        a->probe_left = argc >= 2 ? strtoul(argv[1], NULL, 0) : 12;
        if (!a->probe_left) a->probe_left = 12;
        a->warmup_left = 5;
        set_running(a, 1);
        printf("PROBE START frames=%u\n", a->probe_left);
    }
    else if (!strcmp(argv[0], "capture")) {
        unsigned n = argc >= 2 ? strtoul(argv[1], NULL, 0) : 50;
        const char *path = argc >= 3 ? argv[2] : "/tmp/capture.h264";
        a->cap_limit = argc >= 4 ? (uint64_t)strtoul(argv[3], NULL, 0) * 1024ULL : 8ULL * 1024ULL * 1024ULL;
        if (!capture_open(a, n, path)) set_running(a, 1);
    }
    else if (!strcmp(argv[0], "sensor")) {
        uint32_t intt = 0, gain = 0;
        int ri = fh_sensor_gc1054_get_intt(&a->sensor, &intt);
        int rg = fh_sensor_gc1054_get_gain(&a->sensor, &gain);
        if (argc < 2 || !strcmp(argv[1], "status")) {
            printf("SENSOR_AE intt=%u rc=%d gain=%u rc=%d limits=2..%u/64..%u\n",
                   intt, ri, gain, rg, app_ctx_u16(a->isp_rt.ctx + 0x38),
                   app_ctx_u16(a->isp_rt.ctx + 0x36));
        } else if (!strcmp(argv[1], "set") && argc >= 4) {
            uint32_t ni = strtoul(argv[2], NULL, 0);
            uint32_t ng = strtoul(argv[3], NULL, 0);
            uint32_t imax = ae_effective_intt_max(a);
            uint32_t gmax = app_ctx_u16(a->isp_rt.ctx + 0x36);
            int r1, r2;
            if (ni < 2u || ni > imax || ng < 64u || ng > gmax) {
                printf("SENSOR_AE reject intt=%u gain=%u limits=2..%u/64..%u\n",
                       ni, ng, imax, gmax);
            } else {
                a->ae_auto_enabled = 0;
                r1 = fh_sensor_gc1054_set_intt(&a->sensor, ni);
                r2 = r1 ? -ECANCELED : fh_sensor_gc1054_set_gain(&a->sensor, ng);
                if (r1 || r2) {
                    (void)fh_sensor_gc1054_set_intt(&a->sensor, intt);
                    (void)fh_sensor_gc1054_set_gain(&a->sensor, gain);
                }
                printf("SENSOR_AE set old=%u/%u new=%u/%u rc=%d/%d rollback=%d\n",
                       intt, gain, ni, ng, r1, r2, !!(r1 || r2));
            }
        } else printf("usage: sensor status|set INTT GAIN\n");
    }
    else if (!strcmp(argv[0], "lens")) {
        if (argc < 2 || !strcmp(argv[1], "status")) lens_status(a);
        else if (!strcmp(argv[1], "wide")) (void)lens_switch(a, FH8626_LENS_WIDE);
        else if (!strcmp(argv[1], "tele")) (void)lens_switch(a, FH8626_LENS_TELE);
        else printf("usage: lens status|wide|tele\n");
    }
    else if (!strcmp(argv[0], "scene")) {
        if (argc < 2 || !strcmp(argv[1], "status")) illumination_status(a);
        else if (!strcmp(argv[1], "auto")) (void)scene_auto_enable(a);
        else if (!strcmp(argv[1], "day") || !strcmp(argv[1], "night") ||
                 !strcmp(argv[1], "wlight")) {
            a->scene_auto_enabled = 0;
            (void)scene_switch(a, argv[1]);
        } else printf("usage: scene status|auto|day|night|wlight\n");
    }
    else if (!strcmp(argv[0], "ircut")) {
        if (argc < 2 || !strcmp(argv[1], "status")) illumination_status(a);
        else if (!strcmp(argv[1], "day") || !strcmp(argv[1], "night")) {
            int rc = board_light_command("ircut", argv[1]);
            a->scene_auto_enabled = 0;
            if (!rc) {
                snprintf(a->ircut_state, sizeof(a->ircut_state), "%s", argv[1]);
                snprintf(a->scene, sizeof(a->scene), "manual");
            }else{
                snprintf(a->ircut_state,sizeof(a->ircut_state),"unknown");
                snprintf(a->scene,sizeof(a->scene),"unknown");
            }
            illumination_publish_state(a);
            printf("IRCUT target=%s rc=%d\n", argv[1], rc);
        } else printf("usage: ircut status|day|night\n");
    }
    else if (!strcmp(argv[0], "irled") || !strcmp(argv[0], "white")) {
        int *state = !strcmp(argv[0], "irled") ? &a->ir_led_state : &a->white_led_state;
        const char *group = !strcmp(argv[0], "irled") ? "ir" : "white";
        if (argc < 2 || !strcmp(argv[1], "status")) illumination_status(a);
        else if (!strcmp(argv[1], "on") || !strcmp(argv[1], "off")) {
            int rc = board_light_command(group, argv[1]);
            a->scene_auto_enabled = 0;
            if (!rc) {
                *state = !strcmp(argv[1], "on");
                snprintf(a->scene, sizeof(a->scene), "manual");
            }else{
                *state=-1;
                snprintf(a->scene,sizeof(a->scene),"unknown");
            }
            illumination_publish_state(a);
            printf("ILLUM_LED type=%s target=%s rc=%d\n", argv[0], argv[1], rc);
        } else printf("usage: %s status|on|off\n", argv[0]);
    }
    else if (!strcmp(argv[0], "lightsensor")) {
        if (argc < 2 || !strcmp(argv[1], "status")) illumination_status(a);
        else if (!strcmp(argv[1], "read"))
            printf("LIGHT_SENSOR read rc=%d\n", light_sensor_read(a, 1));
        else printf("usage: lightsensor status|read\n");
    }
    else if (!strcmp(argv[0], "ae")) {
        if (argc < 2 || !strcmp(argv[1], "status")) {
            uint32_t intt = 0, gain = 0;
            int ri = fh_sensor_gc1054_get_intt(&a->sensor, &intt);
            int rg = fh_sensor_gc1054_get_gain(&a->sensor, &gain);
            printf("AE mode=%s target=%u stock_target=%u mean=%u sensor=%u/%u rc=%d/%d updates=%llu failures=%llu source=c6c00-c757c\n",
                   a->ae_auto_enabled ? "auto" : "manual", a->ae_target,
                   a->ae_stock_target, a->ae_last_mean, intt, gain, ri, rg,
                   (unsigned long long)a->ae_updates,
                   (unsigned long long)a->ae_failures);
        } else if (!strcmp(argv[1], "auto")) {
            a->ae_auto_enabled = 1;
            printf("AE mode=auto target=%u\n", a->ae_target);
        } else if (!strcmp(argv[1], "off")) {
            a->ae_auto_enabled = 0;
            printf("AE mode=manual sensor values held\n");
        } else if (!strcmp(argv[1], "target") && argc >= 3) {
            uint32_t target = strtoul(argv[2], NULL, 0);
            if (target < 64u || target > 4095u) {
                printf("AE target rejected=%u limits=64..4095\n", target);
            } else {
                a->ae_target = target;
                a->ae_auto_enabled = 1;
                printf("AE mode=auto target=%u\n", a->ae_target);
            }
        } else if (!strcmp(argv[1], "reset-stock")) {
            a->ae_target = a->ae_stock_target;
            a->ae_auto_enabled = 1;
            printf("AE reset-stock mode=auto target=%u\n", a->ae_target);
        } else printf("usage: ae status|auto|off|target VALUE|reset-stock\n");
    }
    else if (!strcmp(argv[0], "antiflicker")) {
        if (argc < 2 || !strcmp(argv[1], "status")) {
            printf("ANTIFLICKER mode=%s hz=%u quantum_lines=%u stock_selector=%u\n",
                   !a->antiflicker_hz ? "off" :
                   (a->antiflicker_hz == 1u ? "stock" :
                    (a->antiflicker_hz == 50u ? "50hz" : "60hz")),
                   a->antiflicker_hz > 1u ? a->antiflicker_hz : 0u,
                   antiflicker_quantum_lines(a), a->isp_rt.ctx[0xa4c]);
        } else if (!strcmp(argv[1], "off")) {
            a->antiflicker_hz = 0u;
            printf("ANTIFLICKER mode=off quantum_lines=1 next_ae_epoch=1\n");
        } else if (!strcmp(argv[1], "50") || !strcmp(argv[1], "60")) {
            a->antiflicker_hz = (uint32_t)strtoul(argv[1], NULL, 10);
            printf("ANTIFLICKER mode=%shz quantum_lines=%u next_ae_epoch=1\n",
                   argv[1], antiflicker_quantum_lines(a));
        } else if (!strcmp(argv[1], "stock")) {
            a->antiflicker_hz = 1u;
            printf("ANTIFLICKER mode=stock selector=%u quantum_lines=%u next_ae_epoch=1\n",
                   a->isp_rt.ctx[0xa4c], antiflicker_quantum_lines(a));
        } else printf("usage: antiflicker status|off|50|60|stock\n");
    }
    else if (!strcmp(argv[0], "profile")) {
        if (argc < 2 || !strcmp(argv[1], "status")) {
            printf("ISP_PROFILE active=%s source=/usr/share/fh8626/sensor_gc1054_mipi.bin\n",
                   a->isp_profile[0] ? a->isp_profile : "unknown");
        } else if (!strcmp(argv[1], "day") || !strcmp(argv[1], "night") ||
                   !strcmp(argv[1], "wlight")) {
            (void)set_named_profile(a, argv[1]);
        } else printf("usage: profile status|day|night|wlight\n");
    }
    else if (!strcmp(argv[0], "iq")) {
        if (argc < 2 || !strcmp(argv[1], "status")) {
            printf("IQ profile=%s brightness=%u contrast=%u saturation=%u sharpness=%u stock=128\n",
                   a->isp_profile, a->brightness_level, a->contrast_level,
                   a->saturation_level, a->sharpness_level);
        } else if ((!strcmp(argv[1], "brightness") ||
                    !strcmp(argv[1], "contrast") ||
                    !strcmp(argv[1], "saturation") ||
                    !strcmp(argv[1], "sharpness")) && argc >= 3) {
            uint32_t value = strtoul(argv[2], NULL, 0);
            if (value > 255u) {
                printf("IQ %s rejected=%u limits=0..255\n", argv[1], value);
            } else {
                if (!strcmp(argv[1], "brightness")) a->brightness_level = value;
                else if (!strcmp(argv[1], "contrast")) a->contrast_level = value;
                else if (!strcmp(argv[1], "saturation")) a->saturation_level = value;
                else a->sharpness_level = value;
                iq_apply(a);
                printf("IQ %s=%u stock=128 staged=next-runtime-tick\n",
                       argv[1], value);
            }
        } else if (!strcmp(argv[1], "reset")) {
            a->brightness_level = a->contrast_level = 128u;
            a->saturation_level = a->sharpness_level = 128u;
            iq_apply(a);
            printf("IQ reset all=128 profile=%s\n", a->isp_profile);
        } else printf("usage: iq status|brightness|contrast|saturation|sharpness 0..255|reset\n");
    }
    else if (!strcmp(argv[0], "encoder")) {
        struct pae_rc_cfg rc;
        int erc = encoder_rc_get(a, &rc);
        if (erc) printf("ENCODER get rc=%d\n", erc);
        else if (argc < 2 || !strcmp(argv[1], "status")) {
            printf("ENCODER ch=0 mode=%u rate=%u fpspack=%08x initqp=%u I=%u..%u P=%u..%u ipdelta=%d still=%u maxrate=%u maxstillqp=%u\n",
                   rc.rc_mode, rc.bitrate_or_rate, rc.frame_rate_packed,
                   rc.init_qp, rc.i_min_qp, rc.i_max_qp, rc.p_min_qp,
                   rc.p_max_qp, rc.ip_qp_delta, rc.still_rate_percent,
                   rc.max_rate_percent, rc.max_still_qp);
        } else if (!strcmp(argv[1], "config") && argc == 4) {
            char *end_rate;
            uint32_t app_mode, mode;
            unsigned long rate;
            int mode_rc = fh_h264_apollo_rcmode_parse(argv[2], &app_mode);
            if (!mode_rc) mode_rc = fh_h264_app_mode_to_wire(app_mode, &mode);
            errno = 0;
            rate = strtoul(argv[3], &end_rate, 10);
            if (mode_rc || errno || !*argv[3] || *end_rate ||
                argv[3][0] == '-' ||
                (mode != FH_PAE_RC_CBR && mode != FH_PAE_RC_AVBR) ||
                !rate || rate > UINT32_MAX) {
                printf("ENCODER config rejected: cbr/avbr, positive bps\n");
            } else {
                rc.rc_mode = (uint32_t)mode;
                rc.bitrate_or_rate = (uint32_t)rate;
                /* Existing Divinus QP policy, one reconfiguration cycle. */
                rc.init_qp = 32; rc.i_min_qp = 24; rc.i_max_qp = 42;
                rc.p_min_qp = 26; rc.p_max_qp = 42;
                rc.still_rate_percent = 30; rc.max_still_qp = 38;
                (void)encoder_rc_commit(a, &rc);
            }
        } else if (!strcmp(argv[1], "bitrate") && argc >= 3) {
            uint32_t v = strtoul(argv[2], NULL, 0);
            if (!v) printf("ENCODER bitrate rejected=0\n");
            else { rc.bitrate_or_rate = v; (void)encoder_rc_commit(a, &rc); }
        } else if (!strcmp(argv[1], "rcmode") && argc >= 3) {
            uint32_t v = strtoul(argv[2], NULL, 0);
            if (v > 5u) printf("ENCODER rcmode rejected=%u\n", v);
            else { rc.rc_mode = v; (void)encoder_rc_commit(a, &rc); }
        } else if (!strcmp(argv[1], "refresh")) {
            /* Reapplying the accepted full RC block rebuilds SPS/PPS and makes
             * the first picture after restart intra-coded. Used for late join. */
            (void)encoder_rc_commit(a, &rc);
        } else if (!strcmp(argv[1], "qp") && argc >= 7) {
            uint32_t q[5]; unsigned i;
            for (i = 0; i < 5u; ++i) q[i] = strtoul(argv[i + 2], NULL, 0);
            if (q[0] > 51u || q[1] > q[2] || q[3] > q[4] || q[2] > 51u || q[4] > 51u)
                printf("ENCODER qp rejected limits=0..51 min<=max\n");
            else {
                rc.init_qp=q[0]; rc.i_min_qp=q[1]; rc.i_max_qp=q[2];
                rc.p_min_qp=q[3]; rc.p_max_qp=q[4];
                (void)encoder_rc_commit(a, &rc);
            }
        } else printf("usage: encoder status|bitrate BPS|rcmode 0..5|refresh|qp INIT IMIN IMAX PMIN PMAX\n");
    }
    else if (!strcmp(argv[0], "nr3d")) {
        if (argc < 2 || !strcmp(argv[1], "status")) {
            printf("NR3D requested=%d pending=%d warmup=%u ctx11=%02x reg468=%08x saved=%08x\n",
                   !a->nr3d_disabled,a->nr3d_reenable_pending,a->isp_rt.nr3d_warmup_left,
                   a->isp_rt.ctx[0x11], rr(a->r, 0x468),
                   a->nr3d_saved_468);
        } else if (!strcmp(argv[1], "off")) {
            int rc = nr3d_request(a,0);
            printf("NR3D disabled=%d kernel_rc=%d ctx11=%02x reg468=%08x\n",
                   a->nr3d_disabled, rc, a->isp_rt.ctx[0x11], rr(a->r, 0x468));
        } else if (!strcmp(argv[1], "on")) {
            int rc = nr3d_request(a,1);
            printf("NR3D disabled=%d reseed_pending=%d rc=%d reg468=%08x\n",
                   a->nr3d_disabled,a->nr3d_reenable_pending, rc, rr(a->r, 0x468));
        } else printf("usage: nr3d status|on|off\n");
    }
    else if (!strcmp(argv[0], "ltm")) {
        if (argc < 2 || !strcmp(argv[1], "status")) {
            printf("LTM requested=%d hardware_enabled=%d updates=%d ctx260=%02x ctx13=%02x\n",
                   !!(a->isp_rt.ctx[0x260]&2u),!(rr(a->r,0x24)&0x40000u),
                   !!(a->isp_rt.ctx[0x13]&0x20u),a->isp_rt.ctx[0x260],a->isp_rt.ctx[0x13]);
        } else if (!strcmp(argv[1],"on") || !strcmp(argv[1],"off")) {
            int rc=fh_isp_runtime_set_ltm_enabled(&a->isp_rt,!strcmp(argv[1],"on"));
            printf("LTM requested=%d hardware_enabled=%d updates=%d rc=%d\n",
                   !!(a->isp_rt.ctx[0x260]&2u),!(rr(a->r,0x24)&0x40000u),
                   !!(a->isp_rt.ctx[0x13]&0x20u),rc);
        } else if (argc==3 && !strcmp(argv[1],"updates") &&
                   (!strcmp(argv[2],"on") || !strcmp(argv[2],"off"))) {
            if(!strcmp(argv[2],"on"))a->isp_rt.ctx[0x13]|=0x20u;
            else a->isp_rt.ctx[0x13]&=(uint8_t)~0x20u;
            printf("LTM updates=%d hardware_enabled=%d (applies at next accepted tick)\n",
                   !!(a->isp_rt.ctx[0x13]&0x20u),!(rr(a->r,0x24)&0x40000u));
        } else printf("usage: ltm status|on|off|updates on|updates off\n");
    }
    else if (!strcmp(argv[0], "dump")) dump_state(a, argc >= 2 ? argv[1] : "fh");
    else if (!strcmp(argv[0], "reload")) algo_load(a, argc >= 2 ? argv[1] : "/tmp/libfhisp_algo.so");
    else if (!strcmp(argv[0], "unloadalgo")) { algo_unload(a); printf("ALGO unloaded\n"); }
    else if (!strcmp(argv[0], "awbmode1")) {
        if (argc < 2 || !strcmp(argv[1], "status")) awb_mode1_status(a);
        else if (!strcmp(argv[1], "diag")) awb_mode1_diag(a);
        else if (!strcmp(argv[1], "step")) awb_mode1_step(a);
        else if (!strcmp(argv[1], "off") || !strcmp(argv[1], "restore")) {
            a->awb_mode1_enabled = 0;
            awb_mode1_restore(a);
        } else if (!strcmp(argv[1], "on")) {
            a->awb_mode1_enabled = 1;
            printf("AWBMODE1 enabled=1\n");
        } else printf("usage: awbmode1 on|off|diag|step|restore|status\n");
    }
    else if (!strcmp(argv[0], "awbmode0")) {
        if (argc < 2 || !strcmp(argv[1], "status")) {
            printf("AWBMODE0 enabled=%d 224=%08x 228=%08x B0=%04x B1=%02x B2=%02x\n",
                   a->awb_mode0_enabled, rr(a->r,0x224), rr(a->r,0x228),
                   app_ctx_u16(a->isp_rt.ctx+0xb0), a->isp_rt.ctx[0xb1], a->isp_rt.ctx[0xb2]);
        } else if (!strcmp(argv[1], "on")) awb_mode0_enable(a);
        else if (!strcmp(argv[1], "off")) awb_mode0_disable(a);
        else printf("usage: awbmode0 on|off|status\n");
    }
    else if (!strcmp(argv[0], "__disabled_c4998static")) {
        int was=a->running; wr(a->r,0x008,0);
        int rc=fh_isp_runtime_apply_c4998_static_defaults(&a->isp_rt);
        printf("C4998STATIC rc=%d 024=%08x 028=%08x 044=%08x\n",rc,rr(a->r,0x024),rr(a->r,0x028),rr(a->r,0x044));
        if (was) { a->warmup_left=5; wr(a->r,0x008,ISP_MASK); }
    }
    else if (!strcmp(argv[0], "__disabled_knownstock")) {
        int was=a->running; wr(a->r,0x008,0);
        int rc=fh_isp_runtime_apply_known_stock_init(&a->isp_rt,1280,720);
        printf("KNOWNSTOCK rc=%d 024=%08x 028=%08x 044=%08x 270=%08x 3a00=%08x 3ffc=%08x\n",rc,rr(a->r,0x024),rr(a->r,0x028),rr(a->r,0x044),rr(a->r,0x270),rr(a->r,0x3a00),rr(a->r,0x3ffc));
        if (was) { a->warmup_left=10; wr(a->r,0x008,ISP_MASK); }
    }
    else if (!strcmp(argv[0], "base")) {
        int was = a->running;
        wr(a->r, 0x008, 0);
        isp_regs_720p(a->r);
        printf("BASE reapplied 024=%08x 028=%08x\n", rr(a->r,0x024), rr(a->r,0x028));
        if (was) wr(a->r, 0x008, ISP_MASK);
    }
    else printf("UNKNOWN command; use help\n");
}


static int load_raw_profile(struct app *a, const char *path)
{
    unsigned char b[0x0a58];
    FILE *f=fopen(path,"rb");
    size_t n;
    int rc;
    if(!f){ printf("raw profile %s unavailable: %s\n",path,strerror(errno)); return -1; }
    n=fread(b,1,sizeof(b),f); fclose(f);
    if(n!=sizeof(b)){ printf("raw profile short: %u\n",(unsigned)n); return -1; }
    {
        int was_running = a->running;
        if (was_running) wr(a->r, 0x008, 0);
        rc=fh_isp_runtime_load_param(&a->isp_rt,b,n);
        if (!rc && a->isp_rt.stock_runtime_started)
            rc = fh_isp_runtime_apply_profile_luts(&a->isp_rt);
        if (was_running) wr(a->r, 0x008, ISP_MASK);
    }
    if (!rc) {
        a->awb_mode1_saved.valid = 0;
        ae_apply_stock_intt_guard(a);
        ae_refresh_profile_policy(a);
    }
    printf("raw profile load rc=%d size=0x%x dirty=%d fmt0=%02x fmt1=%02x\n",rc,(unsigned)n,a->isp_rt.params_dirty,a->isp_rt.ctx[0x0d],a->isp_rt.ctx[0x0e]);
    return rc;
}

static int load_named_profile(struct app *a, const char *path, const char *name)
{
    unsigned char *b;
    FILE *f = fopen(path, "rb");
    long end;
    size_t n;
    int rc;
    if (!f) {
        printf("SREG container %s unavailable: %s\n", path, strerror(errno));
        return -1;
    }
    if (fseek(f, 0, SEEK_END) || (end = ftell(f)) <= 0 ||
        end > 1024 * 1024 || fseek(f, 0, SEEK_SET)) {
        fclose(f);
        return -1;
    }
    b = malloc((size_t)end);
    if (!b) { fclose(f); return -1; }
    n = fread(b, 1, (size_t)end, f);
    fclose(f);
    if (n != (size_t)end) { free(b); return -1; }
    {
        int was_running = a->running;
        if (was_running) wr(a->r, 0x008, 0);
        rc = fh_isp_runtime_load_sreg_profile(&a->isp_rt, b, n, name);
        if (!rc && a->isp_rt.stock_runtime_started)
            rc = fh_isp_runtime_apply_profile_luts(&a->isp_rt);
        if (was_running) wr(a->r, 0x008, ISP_MASK);
    }
    if (!rc) {
        a->awb_mode1_saved.valid = 0;
        ae_apply_stock_intt_guard(a);
        ae_refresh_profile_policy(a);
    }
    free(b);
    printf("SREG profile=%s load rc=%d container=0x%x dirty=%d\n",
           name, rc, (unsigned)n, a->isp_rt.params_dirty);
    return rc;
}

static int setup(struct app *a)
{
    int ret;
    uint32_t need, chn, mode = 0, q692f = 0;
    uint32_t out_w = 1280u, out_h = 720u;
    const char *output = getenv("FH8626_OUTPUT");
    const char *antiflicker = getenv("FH8626_ANTIFLICKER");
    const char *nr3d = getenv("FH8626_NR3D");
    unsigned char icfg[92];
    if (output && !strcmp(output, "1080p")) { out_w = 1920u; out_h = 1080u; }
    else if (output && !strcmp(output, "360p")) { out_w = 640u; out_h = 360u; }
    else if (output && strcmp(output, "720p")) {
        fprintf(stderr, "Unsupported FH8626_OUTPUT=%s (use 360p|720p|1080p)\n", output);
        return 1;
    }
    a->output_width = out_w; a->output_height = out_h;
    /* Sensor/MIPI must be initialized before touching ISP MMIO.  This is the
     * order proven by gc1054_init_compat and matches the stock service path. */
    fh_isp_runtime_reset(&a->isp_rt);
    a->isp_rt.awb_init=awb_init_triplet;
    a->isp_rt.awb_init_opaque=a;
    if (fh_sensor_gc1054_open(&a->sensor, "/usr/lib/fh8626/libmipi.so", "/usr/lib/fh8626/libgc1054_mipi.so")) {
        printf("GC1054 persistent plug-in open failed\n");
        return 1;
    }
    fh_sensor_gc1054_dump(&a->sensor);
    if (fh8626_dualsensor_board_open(&a->dualsensor, "/dev/gpiowave8")) {
        printf("dual-sensor GPIO backend open failed\n");
        return 1;
    }
    if (fh_isp_runtime_register_sensor(&a->isp_rt, &a->sensor)) {
        printf("GC1054 callback registration failed\n");
        return 1;
    }
    {
        unsigned char viattr[24];
        int sret;
        memset(viattr, 0, sizeof(viattr));
        sret = prepare_sensor_target(a, FH8626_LENS_WIDE);
        if (sret) return 1;
        sret = prepare_sensor_target(a, FH8626_LENS_TELE);
        if (sret) return 1;
        sret = fh8626_dualsensor_select(&a->dualsensor, FH8626_LENS_WIDE,
                                        NULL, NULL);
        printf("SENSOR_TARGET default=wide rc=%d\n", sret);
        if (sret) return 1;
        usleep(600000);
        printf("SENSOR_GET_VI call\n"); fflush(stdout);
        sret = fh_sensor_gc1054_get_vi_attr(&a->sensor, viattr);
        printf("SENSOR_GET_VI ret=%d raw:", sret);
        for (unsigned i = 0; i < sizeof(viattr); ++i) printf(" %02x", viattr[i]);
        printf("\n");
        if (sret) return 1;
        sret = fh_isp_runtime_apply_vi_attr(&a->isp_rt, viattr, sizeof(viattr));
        printf("ISPCTX BF368 rc=%d c14=%04x c16=%04x c18=%04x c1a=%04x c1c=%04x c1e=%04x c20=%04x c22=%04x fmt=%02x/%02x\n",
               sret,
               *(uint16_t *)(void *)(a->isp_rt.ctx + 0x14), *(uint16_t *)(void *)(a->isp_rt.ctx + 0x16),
               *(uint16_t *)(void *)(a->isp_rt.ctx + 0x18), *(uint16_t *)(void *)(a->isp_rt.ctx + 0x1a),
               *(uint16_t *)(void *)(a->isp_rt.ctx + 0x1c), *(uint16_t *)(void *)(a->isp_rt.ctx + 0x1e),
               *(uint16_t *)(void *)(a->isp_rt.ctx + 0x20), *(uint16_t *)(void *)(a->isp_rt.ctx + 0x22),
               a->isp_rt.ctx[0x0d], a->isp_rt.ctx[0x0e]);
        if (sret) return 1;
    }

    printf("SENSOR stage complete; mapping ISP MMIO next\n");
    a->memfd = open("/dev/mem", O_RDWR | O_SYNC);
    if (a->memfd < 0) { perror("open /dev/mem"); return 1; }
    a->r = fh_native_mmap2(NULL, FH_ISP_MMIO_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, a->memfd, FH_ISP_MMIO_PHYS);
    if (a->r == MAP_FAILED) { perror("mmap ISP"); return 1; }
    printf("ISP MMIO mapped phys=%08x size=0x%x virt=%p\n", FH_ISP_MMIO_PHYS, FH_ISP_MMIO_SIZE, (void *)a->r);
    wr(a->r, 0x008, 0);
    if (fh_isp_runtime_attach_mmio(&a->isp_rt, (volatile void *)a->r, FH_ISP_MMIO_SIZE)) {
        printf("ISP runtime attach failed\n");
        return 1;
    }
    {
        const char *profile = getenv("FH8626_ISP_PROFILE");
        if (!profile || !*profile) profile = "day";
        if (strcmp(profile, "day") && strcmp(profile, "night") &&
            strcmp(profile, "wlight")) {
            printf("Unsupported FH8626_ISP_PROFILE=%s\n", profile);
            return 1;
        }
        if (load_named_profile(a, "/usr/share/fh8626/sensor_gc1054_mipi.bin",
                               profile)) {
            if (strcmp(profile, "day") ||
                load_raw_profile(a, "/usr/share/fh8626/gc1054_day.bin"))
                return 1;
            printf("SREG fallback: legacy day payload\n");
        }
        snprintf(a->isp_profile, sizeof(a->isp_profile), "%s", profile);
        a->brightness_level = a->contrast_level = 128u;
        a->saturation_level = a->sharpness_level = 128u;
        iq_capture_stock(a);
    }

    /* Populate the hot-reload ABI only after sensor, MMIO and selected profile are valid.
     * v4.0 forgot this entirely, causing every strict plugin init to receive zeros. */
    memset(&a->algo_host, 0, sizeof(a->algo_host));
    a->algo_host.abi_version = FH_ALGO_ABI_VERSION;
    a->algo_host.regs = a->r;
    a->algo_host.param = a->isp_rt.ctx;
    a->algo_host.param_size = FH_ISP_PARAM_SIZE;
    a->algo_host.sensor = &a->sensor;
    a->algo_host.set_intt = algo_set_intt;
    a->algo_host.set_gain = algo_set_gain;
    a->algo_host.write_reg = algo_write_reg;
    printf("ALGO HOST abi=%u regs=%p param=%p size=0x%x sensor=%p callbacks=%d%d%d\n",
           a->algo_host.abi_version, (void *)a->algo_host.regs,
           (void *)a->algo_host.param, (unsigned)a->algo_host.param_size,
           a->algo_host.sensor, a->algo_host.set_intt != NULL,
           a->algo_host.set_gain != NULL, a->algo_host.write_reg != NULL);

    a->media = open("/dev/media_process", O_RDWR);
    a->isp = open("/dev/isp", O_RDWR);
    a->pae = open("/dev/pae", O_RDWR);
    a->vmm = open("/dev/vmm_userdev", O_RDWR);
    if (a->media < 0 || a->isp < 0 || a->pae < 0 || a->vmm < 0) { perror("open devices"); return 1; }
    printf("fds media=%d isp=%d pae=%d vmm=%d\n", a->media, a->isp, a->pae, a->vmm);
    ret=geometry_exclusive_isp(a);
    if(ret){fprintf(stderr,"GEOMETRY exclusive ISP ownership rejected rc=%d\n",ret);return 1;}
    a->geometry_exclusive_open=1;
    if (alloc_vmm(a->vmm, "isp_cfg", 0x1ce000, &a->imem)) return 2;
    /* Current-Ghidra C67C4/C6934 dataflow plus live nine-zone validation:
     * C67C4(0) is imem+0x1487b8 == provider root+0x48, therefore the mapped
     * ISP runtime root consumed by C6934 (+0x5c8) is imem+0x148770. */
    if (a->imem.size <= 0x148770u ||
        fh_isp_runtime_attach_isp_cfg(&a->isp_rt,
                                      (volatile uint8_t *)(uintptr_t)a->imem.virt + 0x148770u,
                                      a->imem.size - 0x148770u)) {
        fprintf(stderr, "failed to attach isp_cfg mapping to ISP runtime\n");
        return 2;
    }
    memset(icfg, 0, sizeof(icfg));
    put32(icfg, 0x08, a->imem.phys + 0x127500);
    put32(icfg, 0x28, a->imem.phys);
    put32(icfg, 0x48, a->imem.phys + 0x119400);
    if (show("ISP_6921", ioctl(a->isp, ISP_6921, &mode))) return 3;
    /* C1FB8 -> C1EE4 queries actual driver configuration before6920.
     * Keep the captured buffer layout for now; this is not full Init parity.
     * Unsupported/failed query leaves NR3D ineligible but does not stop the
     * independent NR3D-off video pipeline. No constant mode1 fallback. */
    ret=fh_nr3d_read_driver_config(&a->isp_rt,nr3d_query_config,a);
    printf("NR3D_CONFIG query_rc=%d mode=%u eligible_mode=%d\n",ret,
           get32(a->isp_rt.ctx,0x11ac),get32(a->isp_rt.ctx,0x11ac)==1u);
    if (show("ISP_6920", ioctl(a->isp, ISP_6920, icfg))) return 4;
    if (show("ISP_692F", ioctl(a->isp, ISP_692F, &q692f))) return 5;
    if (show("ISP_6919", ioctl(a->isp, ISP_6919, 0))) return 6;
    if (show("ISP_6932", ioctl(a->isp, ISP_6932, 0))) return 7;
    /* Stock lifecycle: one-time ISP core defaults/tables belong before ISP_START.
     * Keep IRQ mask disabled throughout pre-start initialization. */
    isp_regs_720p(a->r);
    ret = fh_isp_runtime_apply_known_stock_init(&a->isp_rt, 1280, 720);
    printf("PRESTART STOCKCTX rc=%d 024=%08x 030=%08x 07c=%08x 080=%08x 17c=%08x 180=%08x 38c=%08x 390=%08x 488=%08x 490=%08x 5c4=%08x\n",
           ret, rr(a->r,0x024), rr(a->r,0x030), rr(a->r,0x07c), rr(a->r,0x080),
           rr(a->r,0x17c), rr(a->r,0x180), rr(a->r,0x38c), rr(a->r,0x390),
           rr(a->r,0x488), rr(a->r,0x490), rr(a->r,0x5c4));
    if (ret) return 8;
    wr(a->r, 0x008, 0);
    need = 0; ret = ioctl(a->isp, VPU_MEM_QUERY, &need); printf("VPU_SYS_QUERY ret=%d need=0x%x\n", ret, need);
    if (ret || alloc_vmm(a->vmm, "vpu_sys", need, &a->vsys)) return 8;
    if (show("VPU_SYS_INIT", ioctl(a->isp, VPU_SYS_MEM_INIT, &a->vsys))) return 9;
    { uint32_t vi[3] = {1280,720,0}; if (show("VPU_SET_VI", ioctl(a->isp, VPU_SET_VI_ATTR, vi))) return 10; }
    a->geometry_startup_ready=1; /* successful SYS_MEM_INIT + native VI */
    if(geometry_setup(a,out_w,out_h))return 11;
    a->geometry_startup_ready=0; /* no subsequent configure in this lifecycle */
    chn = 0; if (show("VPU_OPEN_CH0", ioctl(a->isp,VPU_OPEN_CHN,&chn))) return 14;
    /* SET_CHN_CFG disables frame control. Explicitly restore the current
     * owner native-cadence policy (25/1); full stock16.66 RC preset is separate.
     * GET confirms the accepted ratio, not physical frame delivery. */
    { uint32_t pace[2]={0,FH8626_OWNER_FPS_PACKED},readback[2]={0,0};
      if(show("VPU_SET_FRAMECTRL",ioctl(a->isp,0xc0086954UL,pace)) ||
         show("VPU_GET_FRAMECTRL",ioctl(a->isp,0xc0086955UL,readback)) ||
         readback[0]!=0 || readback[1]!=pace[1])return 14;
      a->geometry.plan.needs_owner_pacing_restore=0;
    }
    if (show("ISP_START", ioctl(a->isp,ISP_START,0))) return 15;
    ret = fh_isp_runtime_apply_profile_luts(&a->isp_rt);
    if (ret) { fprintf(stderr, "post-start CFB64 LUT publication failed: %d\n", ret); return 15; }
    /* Stock performs its one-time CB890 module initialization after start.
     * CFD00 clears only ISP+0x024 bit20.  A former whole-word 0x0a61eb00
     * write destroyed C531C's live format/CFA fields; run the exact masked
     * one-time path instead. */
    ret = fh_isp_runtime_tick_proven_subset(&a->isp_rt);
    if (ret) { fprintf(stderr, "post-start ISP module init failed: %d\n", ret); return 15; }
    wr(a->r,0x008,0);
    printf("POSTSTART RUNTIME 024=%08x 028=%08x\n", rr(a->r,0x024), rr(a->r,0x028));
    need = 0; ret = ioctl(a->pae,PAE_SYS_QUERY,&need); printf("PAE_SYS_QUERY ret=%d need=0x%x\n",ret,need);
    if (ret || alloc_vmm(a->vmm,"pae_sys",need,&a->psys)) return 16;
    if (show("PAE_SYS_INIT",ioctl(a->pae,PAE_SYS_INIT,&a->psys))) return 17;
    { struct pae_mem_q q={0,0,out_w,out_h,0}; ret=ioctl(a->pae,PAE_ENC_MEM_SIZE,&q); printf("PAE_ENC_MEM_SIZE ret=%d size=0x%x\n",ret,q.size); if(ret||alloc_vmm(a->vmm,"pae_enc0",q.size,&a->pch)) return 18; }
    { struct pae_mem m={0,a->pch.phys,a->pch.virt,a->pch.size,out_w,out_h,0}; if(show("PAE_ENC_MEM_INIT",ioctl(a->pae,PAE_ENC_MEM_INIT,&m))) return 19; }
    /* A new startup epoch has no reusable old parameter sets. Driver SET_CONFIG
     * generates aligned input metadata and cropped SPS from VISIBLE sizes. */
    a->sps_len=a->pps_len=0;a->stream_have_ts=0;
    { struct pae_cfg c; memset(&c,0,sizeof(c)); c.chn=0;c.width=out_w;c.height=out_h;c.field0c=50;c.profile=66;c.qp=28;c.fps=FH8626_OWNER_FPS_PACKED; if(show("PAE_SET_CONFIG",ioctl(a->pae,PAE_SET_CONFIG,&c))) return 20; }
    { struct pae_rc_cfg rc;
      struct fh_h264_control control = {owner_h264_ioctl, a, 0};
      encoder_rc_default_main(&rc);
      if(show("PAE_SET_RC_CONFIG",fh_h264_set_rc_cold(&control,&rc))) return 20; }
    { uint32_t bind[2]={1,7}; if(show("MEDIA_BIND 1->7",ioctl(a->media,MEDIA_BIND,bind))) return 21; }
    chn=0; if(show("PAE_ENC_START",ioctl(a->pae,PAE_ENC_START,&chn))) return 22;
    a->venc_active_mask = 1u;
    { uint32_t en=1; if(show("VPU_ENABLE",ioctl(a->isp,VPU_ENABLE,&en))) return 23; }
    wr(a->r,0x008,0);
    a->ae_stock_target = (uint32_t)a->isp_rt.ctx[0x30u] << 4;
    if (!a->ae_stock_target) a->ae_stock_target = 1280u;
    a->ae_target = a->ae_stock_target;
    a->ae_auto_enabled = 1;
    fh8626_ae_runtime_init_passive(&a->ae_rt, a->isp_rt.ctx, a->r,
                                   &a->sensor, NULL, NULL, ae_frontend_timing, a);
    (void)fh8626_ae_runtime_enable_observe(&a->ae_rt, 1);
    ret = fh8626_ae_runtime_enable_commit(&a->ae_rt, 1);
    if (ret) {
        fprintf(stderr, "AE stock runtime enable failed: %d\n", ret);
        return 24;
    }
    a->antiflicker_hz = 50u;
    if (antiflicker && !strcmp(antiflicker, "off")) a->antiflicker_hz = 0u;
    else if (antiflicker && !strcmp(antiflicker, "60")) a->antiflicker_hz = 60u;
    else if (antiflicker && !strcmp(antiflicker, "stock")) a->antiflicker_hz = 1u;
    else if (antiflicker && strcmp(antiflicker, "50")) {
        fprintf(stderr, "Unsupported FH8626_ANTIFLICKER=%s (use off|50|60|stock)\n",
                antiflicker);
        return 24;
    }
    printf("ANTIFLICKER default mode=%s quantum_lines=%u stock_selector=%u\n",
           !a->antiflicker_hz ? "off" :
           (a->antiflicker_hz == 1u ? "stock" :
            (a->antiflicker_hz == 50u ? "50hz" : "60hz")),
           antiflicker_quantum_lines(a), a->isp_rt.ctx[0xa4c]);
    /* The temporal motion/buffer/gain chain is not yet stock-complete.  Do not
     * enable the kernel NR3D engine implicitly: on this target that produces
     * long dark motion trails.  Keep an explicit opt-in for controlled A/B. */
    ret = 0;
    if (!nr3d || !strcmp(nr3d, "off")) {
        ret = nr3d_request(a,0);
        if (ret) return 25;
    } else if (!strcmp(nr3d, "on")) {
        ret=nr3d_request(a,1);
        if(ret)return 25;
    } else {
        fprintf(stderr, "Unsupported FH8626_NR3D=%s (use off|on)\n", nr3d);
        return 25;
    }
    printf("NR3D default=%s pending=%d kernel_rc=%d\n",
           a->nr3d_disabled ? "off" : "on",a->nr3d_reenable_pending,ret);
    return 0;
}

static void unmap_mem3(struct mem3 *m)
{
    if (!m) return;
    if (m->virt && m->size)
        munmap((void *)(uintptr_t)m->virt, m->size);
    memset(m, 0, sizeof(*m));
}

static void owner_recovery_hold(struct app *a)
{
    if(a->r && a->r!=MAP_FAILED)wr(a->r,0x008,0);
    fprintf(stderr,"RECOVERY_REQUIRED: retaining fds/VMM/owner lock; reboot required\n");
    for(;;)pause();
}

static int owner_ioctl_error(int result,int saved_errno)
{
    if(!result)return 0;
    /* Vendor status is nonzero even when libc errno was not set. */
    return result==-1 && saved_errno ? -saved_errno : -EIO;
}

static int owner_shutdown(struct app *a)
{
    uint32_t zero = 0;
    int rc = 0, r;
    if (!a) return -EINVAL;
    printf("SHUTDOWN begin running=%d venc_mask=%x\n",
           a->running, a->venc_active_mask);
    fh8626_sidecar_publisher_close(&a->sidecar);
    capture_close(a);
    a->probe_left = 0;
    a->warmup_left = 0;
    if (a->r && a->r != MAP_FAILED) {
        wr(a->r, 0x008, 0);
        __sync_synchronize();
    }
    a->running = 0;
    if (a->pae >= 0 && (a->venc_active_mask & 1u)) {
        uint32_t ch = 0;
        errno = 0;
        r = ioctl(a->pae, PAE_ENC_STOP, &ch);
        printf("SHUTDOWN PAE_ENC_STOP ch=0 rc=%d errno=%d\n", r, errno);
        if (r && !rc) rc = owner_ioctl_error(r,errno);
        if (!r) a->venc_active_mask &= ~1u;
    }
    if (a->isp >= 0) {
        errno = 0;
        r = ioctl(a->isp, VPU_ENABLE, &zero);
        printf("SHUTDOWN VPU_ENABLE=0 rc=%d errno=%d\n", r, errno);
        if (r && !rc) rc = owner_ioctl_error(r,errno);
    }
    if(rc && a->geometry.state!=FHG_STATE_FRESH)owner_recovery_hold(a);
    algo_unload(a);
    if (a->ctl >= 0) { close(a->ctl); a->ctl = -1; }
    unlink(CTL_PATH);
    unmap_mem3(&a->pch);
    unmap_mem3(&a->psys);
    unmap_mem3(&a->vch);
    unmap_mem3(&a->vsys);
    unmap_mem3(&a->imem);
    if (a->r && a->r != MAP_FAILED) {
        munmap((void *)(uintptr_t)a->r, FH_ISP_MMIO_SIZE);
        a->r = NULL;
    }
    if (a->pae >= 0) { close(a->pae); a->pae = -1; }
    if (a->isp >= 0) { close(a->isp); a->isp = -1; }
    if (a->media >= 0) { close(a->media); a->media = -1; }
    if (a->vmm >= 0) { close(a->vmm); a->vmm = -1; }
    fh8626_dualsensor_board_close(&a->dualsensor);
    fh_sensor_gc1054_close(&a->sensor);
    if (a->memfd >= 0) { close(a->memfd); a->memfd = -1; }
    if (a->owner_lock >= 0) { close(a->owner_lock); a->owner_lock = -1; }
    printf("SHUTDOWN complete rc=%d; process exiting\n", rc);
    fflush(stdout);
    return rc;
}

int main(void)
{
    struct app a;
    struct fh_log_rotation log_rotation;
    const char *sidecar_path;
    const char *algo_path;
    char ibuf[1024];
    size_t used = 0;
    memset(&a, 0, sizeof(a));
    a.awb_mode1_enabled = 1;
    snprintf(a.scene, sizeof(a.scene), "unknown");
    snprintf(a.ircut_state, sizeof(a.ircut_state), "unknown");
    a.ir_led_state=a.white_led_state=-1;
    a.light_sensor_value = -1;
    a.media=a.isp=a.pae=a.vmm=a.memfd=a.ctl=a.owner_lock=-1;
    pthread_mutex_init(&a.algo_mu, NULL);
    pthread_mutex_init(&a.control_mu, NULL);
    setvbuf(stdout,NULL,_IOLBF,0);
    signal(SIGINT,on_signal); signal(SIGTERM,on_signal); signal(SIGHUP,on_signal);
    if (acquire_owner_lock(&a)) return 1;
    {
        int rc=fh_log_rotation_init(&log_rotation,256*1024);
        if(rc){fprintf(stderr,"LOG rotation initialization failed rc=%d\n",rc);return 1;}
        printf("LOG rotation=%d threshold=262144 generations=2 check_ms=1000\n",log_rotation.enabled);
    }
    if (setup(&a)) {
        if(a.geometry.state!=FHG_STATE_FRESH) {
            /* Failed SET/copy-back may have registered DMA memory. Do not
             * exit (which releases VMM), retry, or run ordinary free paths.
             * Reboot/recovery is required; there is deliberately no READY. */
            owner_recovery_hold(&a);
        }
        return 1;
    }
    sidecar_path = getenv("FH8626_STREAM_SOCKET");
    if (sidecar_path && *sidecar_path) {
        if (fh8626_sidecar_publisher_init(&a.sidecar, sidecar_path,
                4U * 1024U * 1024U) != 0) {
            printf("SIDECAR init path=%s failed errno=%d; graceful startup abort\n",
                   sidecar_path, errno);
            goto startup_failed;
        } else {
            printf("SIDECAR listening path=%s max_payload=%u pts=fixed-25fps\n",
                   sidecar_path, 4U * 1024U * 1024U);
        }
    }
    unlink(CTL_PATH);
    if (mkfifo(CTL_PATH, 0666) && errno != EEXIST) { perror("mkfifo"); goto startup_failed; }
    a.ctl = open(CTL_PATH, O_RDWR | O_NONBLOCK);
    if (a.ctl < 0) { perror("open ctl"); goto startup_failed; }
    algo_path = getenv("FH8626_ALGO_PLUGIN");
    if (algo_path && *algo_path && access(algo_path, R_OK) == 0)
        algo_load(&a, algo_path);
    (void)scene_auto_enable(&a);
    set_running(&a, 1);
    if (pthread_create(&a.control_thread, NULL, isp_control_thread_main, &a) != 0) {
        perror("pthread_create ISP control");
        (void)owner_shutdown(&a);
        pthread_mutex_destroy(&a.control_mu);
        pthread_mutex_destroy(&a.algo_mu);
        return 1;
    }
    a.control_thread_started = 1;
    printf("READY v4.3.0-isp-iq owner-kept=1 native=1280x720 isp-mmio=0x4000 sensor-persistent=1 isp-thread=1 algo-reload=1 ring-wrap=1 ae=1 profiles=3 iq=4 sidecar=%d ctl=%s\n",
           a.sidecar.initialized, CTL_PATH);
    help();
    status(&a);
    for (;;) {
        int log_rc=fh_log_rotation_tick(&log_rotation,monotonic_ms());
        if(log_rc && (log_rotation.failures<=3 || log_rotation.failures%60==0))
            fprintf(stderr,"LOG rotation failed rc=%d count=%u\n",log_rc,log_rotation.failures);
        if (want_safe_hold) {
            want_safe_hold = 0;
            pthread_mutex_lock(&a.control_mu);
            set_running(&a,0);
            pthread_mutex_unlock(&a.control_mu);
            printf("SIGNAL -> SAFE HOLD; owner remains alive\n");
        }
        struct pollfd pfd = { a.ctl, POLLIN, 0 };
        int pr = poll(&pfd, 1, 5);
        if (pr > 0 && (pfd.revents & POLLIN)) {
            ssize_t n = read(a.ctl, ibuf + used, sizeof(ibuf) - 1 - used);
            if (n > 0) {
                used += (size_t)n; ibuf[used] = 0;
                char *start = ibuf;
                for (;;) {
                    char *nl = strchr(start, '\n');
                    if (!nl) break;
                    *nl = 0; command_enqueue(&a,start); start = nl + 1;
                }
                if (start != ibuf) { used = strlen(start); memmove(ibuf,start,used+1); }
                if (used == sizeof(ibuf)-1) used=0;
            }
        }
        pthread_mutex_lock(&a.control_mu);
        command_drain(&a);
        (void)scene_auto_step(&a, 0);
        pthread_mutex_unlock(&a.control_mu);
        if (a.shutdown_requested) {
            a.control_thread_stop = 1;
            if (a.control_thread_started) pthread_join(a.control_thread, NULL);
            int src = owner_shutdown(&a);
            pthread_mutex_destroy(&a.control_mu);
            pthread_mutex_destroy(&a.algo_mu);
            return src ? 2 : 0;
        }
        /* Keep encoder queue drained even in HOLD. IRQ mask state is independent. */
        stream_once(&a);
    }
startup_failed:
    (void)owner_shutdown(&a);
    pthread_mutex_destroy(&a.control_mu);
    pthread_mutex_destroy(&a.algo_mu);
    return 1;
}
