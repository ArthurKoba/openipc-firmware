#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

/*
 * FH8852V200 libacw_mpi-compatible facade over the recovered FH8626 RTX/ARC
 * audio transport.
 *
 * Static donor analysis confirms that the FH8852 public MPI is a thin wrapper
 * around the same RTX command family already hardware-proven on AJL33PQ0866:
 *   reset ioctl                  0x40000000
 *   command ioctl                0x20000000
 *   init                         0x01040004
 *   set config                   0x01040005
 *   AI frame                     0x01008000
 *   AO frame                     0x01008002
 * and the same simple command IDs used by the recovered FH8626 path.
 *
 * This facade deliberately implements only the recovered public subset. AEC,
 * AGC and advanced NR entry points remain explicit -ENOSYS boundaries until
 * their exact command records are required by an observed Majestic call.
 */

#define RTXBUS_RESET   0x40000000UL
#define RTXBUS_COMMAND 0x20000000UL

#define AC_CMD_AO_WAIT        3u
#define AC_CMD_AI_ENABLE      6u
#define AC_CMD_AI_DISABLE     7u
#define AC_CMD_AO_ENABLE      8u
#define AC_CMD_AO_MODE        9u
#define AC_CMD_AO_DISABLE     10u
#define AC_CMD_AI_VOLUME      15u
#define AC_CMD_AI_MICIN_VOL   16u
#define AC_CMD_AO_VOLUME      17u
#define AC_CMD_WORK_MODE      22u
#define AC_CMD_AI_ANALOG_VOL  26u
#define AC_CMD_AI_DIGITAL_VOL 27u
#define AC_CMD_AO_DIGITAL_VOL 28u
#define AC_CMD_AI_CLEAR       31u
#define AC_CMD_AO_CLEAR       32u

#define FH_AC_E_ARGUMENT ((int)0x8013000fU)
#define FH_AC_E_RANGE    ((int)0x8013000eU)
#define FH_AC_E_NOMEM    ((int)0x80130012U)

struct ac_init_command {
    uint32_t size;
    uint16_t size_a;
    uint16_t size_b;
    uint32_t opcode;
    int32_t status;
    uint32_t reserved;
    uint32_t map_offset;
    uint32_t map_length;
    uint32_t tail_length;
};

struct ac_simple_command {
    uint32_t size;
    uint16_t size_a;
    uint16_t size_b;
    uint32_t opcode;
    int32_t status;
    uint32_t value;
};

struct ac_config_command {
    uint32_t size;
    uint16_t size_a;
    uint16_t size_b;
    uint32_t opcode;
    int32_t status;
    uint32_t config[7];
    uint32_t selector;
};

struct ac_frame_command {
    uint32_t size;
    uint16_t size_a;
    uint16_t size_b;
    uint32_t opcode;
    int32_t status;
    uint32_t data_length;
    uint32_t data_offset;
    uint32_t pts_low;
    uint32_t pts_high;
};

struct ac_init_params_command {
    uint32_t size;
    uint16_t size_a;
    uint16_t size_b;
    uint32_t opcode;
    int32_t status;
    uint32_t reserved;
    uint32_t blob_length;
    uint8_t blob[0x17a];
};

/* FH8852 public frame object recovered from AI/AO wrappers. */
struct fh_ac_frame {
    uint32_t length;
    void *data;
};

_Static_assert(sizeof(struct ac_init_command) == 0x24, "AC init wire size");
_Static_assert(sizeof(struct ac_simple_command) == 0x14, "AC simple wire size");
_Static_assert(sizeof(struct ac_config_command) == 0x30, "AC config wire size");
_Static_assert(sizeof(struct ac_frame_command) == 0x24, "AC frame storage size");
_Static_assert(sizeof(struct ac_init_params_command) == 0x192, "AC init-param storage size");

static int ac_fd = -1;
static uint8_t *ac_map = MAP_FAILED;
static uint32_t ac_map_offset;
static uint32_t ac_map_length;
static uint32_t ac_tail_length;
static uint8_t *ac_ao_buffer;
static uint32_t ac_ao_offset;

static int trace_enabled(void)
{
    const char *v = getenv("FH8626_MAJESTIC_TRACE");
    return v && v[0] && strcmp(v, "0");
}

static int command(void *record)
{
    if (ac_fd < 0)
        return FH_AC_E_ARGUMENT;
    errno = 0;
    if (ioctl(ac_fd, RTXBUS_COMMAND, record) < 0)
        return errno ? -errno : -EIO;
    return 0;
}

static int simple(uint32_t id, uint32_t value)
{
    struct ac_simple_command r = {
        .size = 12,
        .size_a = 12,
        .size_b = 12,
        .opcode = 0x01000000u | id | (id == AC_CMD_AO_WAIT ? 0u : 0x00040000u),
        .value = value,
    };
    int rc = command(&r);
    return rc ? rc : r.status;
}

static int unsupported(const char *name)
{
    if (trace_enabled())
        fprintf(stderr, "fh8626-acw-compat: unsupported %s\n", name);
    return -ENOSYS;
}

int FH_AC_Init(void)
{
    struct ac_init_command r = {
        .size = 0x18,
        .size_a = 0x18,
        .size_b = 0x18,
        .opcode = 0x01040004u,
    };
    int rc;

    if (ac_fd >= 0)
        return 0;

    ac_fd = open("/dev/rtxbus", O_RDWR | O_CLOEXEC);
    if (ac_fd < 0)
        return errno ? -errno : -EIO;

    if (ioctl(ac_fd, RTXBUS_RESET, 0) < 0) {
        rc = errno ? -errno : -EIO;
        goto fail;
    }
    rc = command(&r);
    if (rc)
        goto fail;
    if (r.status) {
        rc = r.status;
        goto fail;
    }
    if (!r.map_length || r.tail_length > r.map_length) {
        rc = FH_AC_E_RANGE;
        goto fail;
    }

    ac_map = mmap(NULL, r.map_length, PROT_READ | PROT_WRITE, MAP_SHARED,
                  ac_fd, (off_t)r.map_offset);
    if (ac_map == MAP_FAILED) {
        rc = errno ? -errno : -EIO;
        goto fail;
    }

    ac_map_offset = r.map_offset;
    ac_map_length = r.map_length;
    ac_tail_length = r.tail_length;
    ac_ao_buffer = ac_map + (ac_map_length - ac_tail_length);
    ac_ao_offset = ac_map_offset + (ac_map_length - ac_tail_length);

    if (trace_enabled())
        fprintf(stderr,
            "fh8626-acw-compat: init map=%08x+%u ao=%08x+%u\n",
            ac_map_offset, ac_map_length, ac_ao_offset, ac_tail_length);
    return 0;

fail:
    if (ac_fd >= 0)
        close(ac_fd);
    ac_fd = -1;
    return rc;
}

int FH_AC_DeInit(void)
{
    if (ac_map != MAP_FAILED) {
        munmap(ac_map, ac_map_length);
        ac_map = MAP_FAILED;
    }
    ac_map_offset = ac_map_length = ac_tail_length = ac_ao_offset = 0;
    ac_ao_buffer = NULL;
    if (ac_fd >= 0)
        close(ac_fd);
    ac_fd = -1;
    return 0;
}

int FH_AC_Set_InitParam(const void *blob)
{
    struct ac_init_params_command r = {
        .size = 0x18a,
        .size_a = 0x18a,
        .size_b = 0x18a,
        .opcode = 0x01040022u,
        .blob_length = 0x17a,
    };
    int rc;

    if (!blob)
        return FH_AC_E_ARGUMENT;
    memcpy(r.blob, blob, sizeof(r.blob));
    rc = command(&r);
    return rc ? rc : r.status;
}

int FH_AC_Set_Config_Ext(const uint32_t config[7], uint32_t selector)
{
    struct ac_config_command r = {
        .size = 8,
        .size_a = 8,
        .size_b = 0x28,
        .opcode = 0x01040005u,
        .selector = selector,
    };
    int rc;

    if (!config)
        return FH_AC_E_ARGUMENT;
    memcpy(r.config, config, sizeof(r.config));
    rc = command(&r);
    return rc ? rc : r.status;
}

int FH_AC_Set_Config(const uint32_t config[7])
{
    return FH_AC_Set_Config_Ext(config, 4u);
}

static int ai_frame_fast(struct fh_ac_frame *frame, uint64_t *pts)
{
    struct ac_frame_command r = {
        .size = 0x18,
        .size_a = 0x18,
        .size_b = 8,
        .opcode = 0x01008000u,
    };
    uint32_t relative;
    int rc;

    if (!frame)
        return FH_AC_E_ARGUMENT;
    frame->length = 0;
    frame->data = NULL;

    rc = command(&r);
    if (rc)
        return rc;
    if (r.status)
        return r.status;
    if (!r.data_length)
        return 0;
    if (ac_map == MAP_FAILED || r.data_offset < ac_map_offset)
        return FH_AC_E_RANGE;

    relative = r.data_offset - ac_map_offset;
    if (relative > ac_map_length ||
        r.data_length > ac_map_length - relative)
        return FH_AC_E_RANGE;

    frame->length = r.data_length;
    frame->data = ac_map + relative;
    if (pts)
        *pts = ((uint64_t)r.pts_high << 32) | r.pts_low;
    return 0;
}

int FH_AC_AI_GetFrameWithPtsFast(struct fh_ac_frame *frame, uint64_t *pts)
{
    return ai_frame_fast(frame, pts);
}

int FH_AC_AI_GetFrame(struct fh_ac_frame *frame)
{
    return ai_frame_fast(frame, NULL);
}

int FH_AC_AI_GetFrameWithPts(struct fh_ac_frame *frame, uint64_t *pts)
{
    struct fh_ac_frame native;
    int rc;

    if (!frame || !frame->data)
        return FH_AC_E_ARGUMENT;
    rc = ai_frame_fast(&native, pts);
    if (rc || !native.length)
        return rc;
    memcpy(frame->data, native.data, native.length);
    frame->length = native.length;
    return 0;
}

int FH_AC_AO_SendFrame(const struct fh_ac_frame *frame)
{
    struct ac_frame_command r = {
        .size = 16,
        .size_a = 16,
        .size_b = 16,
        .opcode = 0x01008002u,
    };
    int rc;

    if (!frame || !frame->data)
        return FH_AC_E_ARGUMENT;
    if (!frame->length || (frame->length & 1u) ||
        !ac_tail_length || frame->length > ac_tail_length ||
        !ac_ao_buffer)
        return FH_AC_E_RANGE;

    memcpy(ac_ao_buffer, frame->data, frame->length);
    r.data_length = frame->length;
    r.data_offset = ac_ao_offset;
    rc = command(&r);
    return rc ? rc : r.status;
}

int FH_AC_AI_Enable(void) { return simple(AC_CMD_AI_ENABLE, 0); }
int FH_AC_AI_Disable(void) { return simple(AC_CMD_AI_DISABLE, 0); }
int FH_AC_AO_Enable(void) { return simple(AC_CMD_AO_ENABLE, 0); }
int FH_AC_AO_Disable(void) { return simple(AC_CMD_AO_DISABLE, 0); }
int FH_AC_AI_SetVol(uint32_t v) { return simple(AC_CMD_AI_VOLUME, v); }
int FH_AC_AI_MICIN_SetVol(uint32_t v) { return simple(AC_CMD_AI_MICIN_VOL, v); }
int FH_AC_AO_SetVol(uint32_t v) { return simple(AC_CMD_AO_VOLUME, v); }
int FH_AC_AO_Set_Mode(uint32_t v) { return simple(AC_CMD_AO_MODE, v); }
int FH_AC_Set_WorkMode(uint32_t v) { return simple(AC_CMD_WORK_MODE, v); }
int FH_AC_AI_ClearBuf(void) { return simple(AC_CMD_AI_CLEAR, 0); }
int FH_AC_AO_ClearBuf(void) { return simple(AC_CMD_AO_CLEAR, 0); }
int FH_AC_AO_WaitPlayComplete(void) { return simple(AC_CMD_AO_WAIT, 0); }
int FH_AC_AO_SetDigitalVol(uint32_t v) { return simple(AC_CMD_AO_DIGITAL_VOL, v); }

/* Channel-volume packing in the donor is recovered; preserve it verbatim. */
int FH_AC_AI_CH_SetDigitalVol(uint32_t chn, uint32_t vol)
{
    return simple(AC_CMD_AI_DIGITAL_VOL, ((chn & 0xffffu) << 16) | (vol & 0xffu));
}

int FH_AC_AI_CH_SetAnologVol(uint32_t chn, uint32_t vol)
{
    return simple(AC_CMD_AI_ANALOG_VOL,
        ((chn & 0xffffu) << 16) | (vol & 0xffu));
}

/* Advanced DSP policies are deliberately not claimed by the compatibility layer. */
int FH_AC_AEC_SetConfig(void *p) { (void)p; return unsupported("FH_AC_AEC_SetConfig"); }
int FH_AC_AEC_Change_DefConfig(void *p) { (void)p; return unsupported("FH_AC_AEC_Change_DefConfig"); }
int FH_AC_AEC_Set_fixedDelay(uint32_t v) { (void)v; return unsupported("FH_AC_AEC_Set_fixedDelay"); }
int FH_AC_Agc_SetConfig(void *p) { (void)p; return unsupported("FH_AC_Agc_SetConfig"); }
int FH_AC_Agc_SetConfigExt(void *p) { (void)p; return unsupported("FH_AC_Agc_SetConfigExt"); }
int FH_AC_NR_SetConfig(void *p) { (void)p; return unsupported("FH_AC_NR_SetConfig"); }
int FH_AC_PlayAgc_SetConfigExt(void *p) { (void)p; return unsupported("FH_AC_PlayAgc_SetConfigExt"); }
int FH_AC_PlayNR_SetConfig(void *p) { (void)p; return unsupported("FH_AC_PlayNR_SetConfig"); }

const char *FH_AC_Version(void)
{
    return "fh8626-rtx-compat";
}
