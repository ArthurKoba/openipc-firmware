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
 * The recovered public MPI surface is translated onto the same RTX command
 * family: capture/playback, AEC, AGC, capture/playback NR, HPF, pause/resume,
 * raw capture and variable extension commands. Unsupported behavior is kept
 * explicit rather than reported as a successful no-op.
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
#define AC_CMD_AI_PAUSE       11u
#define AC_CMD_AI_RESUME      12u
#define AC_CMD_AO_PAUSE       13u
#define AC_CMD_AO_RESUME      14u
#define AC_CMD_AEC_CONFIG     18u
#define AC_CMD_NR_CONFIG      19u
#define AC_CMD_PLAY_NR_CONFIG 20u
#define AC_CMD_AGC_CONFIG     21u
#define AC_CMD_EXT            23u
#define AC_CMD_RAW_CONFIG     24u
#define AC_CMD_BIND           25u
#define AC_CMD_AI_BUFSIZE     29u
#define AC_CMD_AO_BUFSIZE     30u
#define AC_CMD_AI_AO_SYNC     33u

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

struct fh_ac_raw_frame {
    void *data;
    uint32_t length;
};

_Static_assert(sizeof(struct ac_init_command) == 0x20, "AC init storage size");
_Static_assert(sizeof(struct ac_simple_command) == 0x14, "AC simple storage size");
_Static_assert(sizeof(struct ac_config_command) == 0x30, "AC config storage size");
_Static_assert(sizeof(struct ac_frame_command) == 0x20, "AC frame storage size");
_Static_assert(sizeof(struct ac_init_params_command) == 0x194, "AC init-param storage size");

static int ac_fd = -1;
static uint8_t *ac_map = MAP_FAILED;
static uint32_t ac_map_offset;
static uint32_t ac_map_length;
static uint32_t ac_tail_length;
static uint8_t *ac_ao_buffer;
static uint32_t ac_ao_offset;
static int ac_ai_enabled;
static int ac_ao_enabled;

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


static int payload_command(uint32_t id, const void *payload, uint32_t len)
{
    uint8_t stack_record[96];
    uint8_t *record = stack_record;
    uint32_t storage = len + 16u;
    uint32_t logical = len + 8u;
    uint32_t opcode = 0x01040000u | id;
    int32_t status = 0;
    int rc;

    if (!payload || !len || ac_fd < 0)
        return FH_AC_E_ARGUMENT;
    if (id == AC_CMD_EXT && (int32_t)((const uint32_t *)payload)[0] < 0)
        opcode |= 0x00018000u;

    if (storage > sizeof(stack_record)) {
        record = malloc(storage);
        if (!record)
            return FH_AC_E_NOMEM;
    }
    memset(record, 0, storage);
    memcpy(record + 0, &logical, 4);
    *(uint16_t *)(record + 4) = (uint16_t)logical;
    *(uint16_t *)(record + 6) = (uint16_t)logical;
    memcpy(record + 8, &opcode, 4);
    memcpy(record + 16, payload, len);

    rc = command(record);
    if (!rc)
        memcpy(&status, record + 12, 4);
    if (record != stack_record)
        free(record);
    return rc ? rc : status;
}

static int query_value_command(uint32_t id, uint32_t *value)
{
    struct ac_simple_command r = {
        .size = 12,
        .size_a = 12,
        .size_b = 12,
        .opcode = 0x01040000u | id,
        .value = 0,
    };
    int rc;

    if (!value)
        return FH_AC_E_ARGUMENT;
    rc = command(&r);
    if (rc)
        return rc;
    if (r.status)
        return r.status;
    *value = r.value;
    return 0;
}

static int ac_init_common(uint32_t external_codec)
{
    struct ac_init_command r = {
        .size = 0x18,
        .size_a = 0x18,
        .size_b = 0x18,
        .opcode = 0x01040004u,
        .reserved = external_codec,
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

    ac_ai_enabled = 0;
    ac_ao_enabled = 0;
    ac_map_offset = r.map_offset;
    ac_map_length = r.map_length;
    ac_tail_length = r.tail_length;
    ac_ao_buffer = ac_map + (ac_map_length - ac_tail_length);
    ac_ao_offset = ac_map_offset + (ac_map_length - ac_tail_length);

    if (trace_enabled())
        fprintf(stderr,
            "fh8626-acw-compat: init external=%u map=%08x+%u ao=%08x+%u\n",
            external_codec, ac_map_offset, ac_map_length,
            ac_ao_offset, ac_tail_length);
    return 0;

fail:
    if (ac_fd >= 0)
        close(ac_fd);
    ac_fd = -1;
    return rc;
}

int FH_AC_Init(void)
{
    return ac_init_common(0u);
}

int FH_AC_Init_WithExternalCodec(void)
{
    return ac_init_common(1u);
}

int FH_AC_DeInit(void)
{
    int first_error = 0;
    int rc;

    /*
     * Keep the RTX transport balanced. The hardware-proven platform path
     * disables active directions before unmapping/closing /dev/rtxbus.
     * Physical speaker-amplifier mute remains board-owned and is not handled
     * inside this generic FH_AC compatibility library.
     */
    if (ac_fd >= 0 && ac_ai_enabled) {
        rc = simple(AC_CMD_AI_DISABLE, 0);
        if (rc && !first_error)
            first_error = rc;
        else if (!rc)
            ac_ai_enabled = 0;
    }
    if (ac_fd >= 0 && ac_ao_enabled) {
        rc = simple(AC_CMD_AO_DISABLE, 0);
        if (rc && !first_error)
            first_error = rc;
        else if (!rc)
            ac_ao_enabled = 0;
    }

    if (ac_map != MAP_FAILED) {
        munmap(ac_map, ac_map_length);
        ac_map = MAP_FAILED;
    }
    ac_map_offset = ac_map_length = ac_tail_length = ac_ao_offset = 0;
    ac_ao_buffer = NULL;
    if (ac_fd >= 0)
        close(ac_fd);
    ac_fd = -1;
    ac_ai_enabled = 0;
    ac_ao_enabled = 0;
    return first_error;
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
    if (!ac_ai_enabled)
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
    if (!ac_ao_enabled)
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

int FH_AC_AI_Enable(void)
{
    int rc = simple(AC_CMD_AI_ENABLE, 0);
    if (!rc)
        ac_ai_enabled = 1;
    return rc;
}

int FH_AC_AI_Disable(void)
{
    int rc;
    if (!ac_ai_enabled)
        return 0;
    rc = simple(AC_CMD_AI_DISABLE, 0);
    if (!rc)
        ac_ai_enabled = 0;
    return rc;
}

int FH_AC_AO_Enable(void)
{
    int rc = simple(AC_CMD_AO_ENABLE, 0);
    if (!rc)
        ac_ao_enabled = 1;
    return rc;
}

int FH_AC_AO_Disable(void)
{
    int rc;
    if (!ac_ao_enabled)
        return 0;
    rc = simple(AC_CMD_AO_DISABLE, 0);
    if (!rc)
        ac_ao_enabled = 0;
    return rc;
}
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

int FH_AC_AEC_SetConfig(const void *p)
{
    return payload_command(AC_CMD_AEC_CONFIG, p, 8u);
}

int FH_AC_NR_SetConfig(const void *p)
{
    return payload_command(AC_CMD_NR_CONFIG, p, 8u);
}

int FH_AC_PlayNR_SetConfig(const void *p)
{
    return payload_command(AC_CMD_PLAY_NR_CONFIG, p, 8u);
}

int FH_AC_Agc_SetConfig(const void *p)
{
    return payload_command(AC_CMD_AGC_CONFIG, p, 8u);
}

int FH_AC_Ext_Ioctl(const void *p)
{
    return payload_command(AC_CMD_EXT, p, 40u);
}

int FH_AC_AEC_Change_DefConfig(uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t ext[10] = {0};
    ext[0] = 4u;
    ext[2] = a;
    ext[3] = b;
    ext[4] = c;
    return FH_AC_Ext_Ioctl(ext);
}

int FH_AC_AEC_Set_fixedDelay(uint32_t v)
{
    uint32_t ext[10] = {0};
    ext[0] = 7u;
    ext[2] = v;
    return FH_AC_Ext_Ioctl(ext);
}

int FH_AC_Agc_SetConfigExt(uint32_t a, uint32_t b,
                           uint32_t c, uint32_t d)
{
    uint32_t cfg[2];
    cfg[0] = a;
    cfg[1] = (d | 0x8000u | (c << 8) | ((b << 7) & 0xffu));
    return FH_AC_Agc_SetConfig(cfg);
}

int FH_AC_PlayAgc_SetConfigExt(uint32_t a, uint32_t b,
                               uint32_t c, uint32_t d)
{
    uint32_t ext[10] = {0};
    ext[0] = 8u;
    ext[2] = d | (c << 8) | (b << 16) | (a << 24);
    return FH_AC_Ext_Ioctl(ext);
}

int FH_AC_AI_HPF_Ctrl(uint32_t v)
{
    uint32_t ext[10] = {0};
    ext[0] = 2u;
    ext[2] = v;
    return FH_AC_Ext_Ioctl(ext);
}

int FH_AC_AO_HPF_Ctrl(uint32_t v)
{
    uint32_t ext[10] = {0};
    ext[0] = 3u;
    ext[2] = v;
    return FH_AC_Ext_Ioctl(ext);
}

int FH_AC_AI_PowerDown_MicBias(uint32_t v)
{
    uint32_t ext[10] = {0};
    ext[0] = 6u;
    ext[2] = v;
    return FH_AC_Ext_Ioctl(ext);
}

int FH_AC_AI_Pause(void) { return simple(AC_CMD_AI_PAUSE, 0); }
int FH_AC_AI_Resume(void) { return simple(AC_CMD_AI_RESUME, 0); }
int FH_AC_AO_Pause(void) { return simple(AC_CMD_AO_PAUSE, 0); }
int FH_AC_AO_Resume(void) { return simple(AC_CMD_AO_RESUME, 0); }
int FH_AC_AI_AO_SYNC_Enable(void) { return simple(AC_CMD_AI_AO_SYNC, 0); }

int FH_AC_AI_QueryBufSize(uint32_t *v)
{
    return query_value_command(AC_CMD_AI_BUFSIZE, v);
}

int FH_AC_AO_QueryBufSize(uint32_t *v)
{
    return query_value_command(AC_CMD_AO_BUFSIZE, v);
}

/* Stock AJL bind policy is -1; command 25 is not part of retail parity. */
int FH_AC_AI_Bind(void) { return -ENOTSUP; }

int FH_AC_AI_HPF_ChangeCoff(const int16_t *coeff, uint32_t count)
{
    uint32_t ext[10] = {0};

    if (!coeff || count < 1u || count > 16u)
        return FH_AC_E_ARGUMENT;
    ext[0] = 9u;
    ext[1] = count;
    memcpy(&ext[2], coeff, count * sizeof(*coeff));
    return FH_AC_Ext_Ioctl(ext);
}

int FH_AC_Raw_SetConfig(const void *config)
{
    return payload_command(AC_CMD_RAW_CONFIG, config, 16u);
}

int FH_AC_Ext2_Ioctl(const void *payload)
{
    const uint32_t *p = payload;
    uint8_t *record;
    uint32_t payload_len, logical, storage;
    int32_t status = 0;
    int rc;

    if (!payload || ac_fd < 0)
        return FH_AC_E_ARGUMENT;

    /*
     * Exact FH8852 donor wrapper:
     *   p[1]             = variable extension payload length
     *   record.size      = payload_len + 0x10
     *   record.size_a/b  = low16(record.size)
     *   record.opcode    = 0x01040022
     *   record+0x10      = original {header(8),payload(payload_len)}
     * Allocation is payload_len + 0x18 because the copied public record
     * includes its own leading 8-byte header.
     */
    payload_len = p[1];
    if (payload_len > 0xffefu)
        return FH_AC_E_RANGE;
    logical = payload_len + 0x10u;
    storage = payload_len + 0x18u;

    record = malloc(storage);
    if (!record)
        return FH_AC_E_NOMEM;
    memset(record, 0, storage);

    memcpy(record + 0, &logical, 4);
    *(uint16_t *)(record + 4) = (uint16_t)logical;
    *(uint16_t *)(record + 6) = (uint16_t)logical;
    *(uint32_t *)(record + 8) = 0x01040022u;
    memcpy(record + 16, payload, payload_len + 8u);

    rc = command(record);
    if (!rc)
        memcpy(&status, record + 12, sizeof(status));
    free(record);
    return rc ? rc : status;
}

int FH_AC_Raw_GetFrameFast(struct fh_ac_raw_frame *frame)
{
    struct ac_frame_command r = {
        .size = 16,
        .size_a = 16,
        .size_b = 8,
        .opcode = 0x01008001u,
    };
    uint32_t relative;
    int rc;

    if (!frame)
        return FH_AC_E_ARGUMENT;
    frame->data = NULL;
    frame->length = 0;

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

    frame->data = ac_map + relative;
    frame->length = r.data_length;
    return 0;
}

const char *FH_AC_Version(void)
{
    return "fh8626-rtx-compat";
}


/* No known FH8852 ACW loader stubs remain in the recovered Majestic surface. */
