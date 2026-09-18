#define _GNU_SOURCE
#include "fh8626-gc1054-native-contract.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

/*
 * Source-native FH8626 GC1054 sensor plug-in.
 *
 * This is an implementation of the recovered 0x68-byte FH8626 callback
 * contract used behind the Majestic FH8852-shaped facade.  Only reverse-
 * confirmed callbacks are populated. Unknown slots remain NULL.
 */

struct i2c_msg_local {
    uint16_t addr;
    uint16_t flags;
    uint16_t len;
    uint16_t pad;
    uint8_t *buf;
};

struct i2c_rdwr_local {
    struct i2c_msg_local *msgs;
    uint32_t nmsgs;
};

#define I2C_M_RD 0x0001u

static int sensor_fd = -1;
static uint32_t current_format = FH8626_GC1054_FORMAT_720P25;
static uint32_t cached_gain = 0x40u;
static uint32_t cached_intt = 0xd0u;
static uint32_t current_frame_length = 899u;
static uint32_t orientation_mode;

extern void mipi_init(int *words);

static int sensor_device_init(void)
{
    if (sensor_fd >= 0)
        close(sensor_fd);

    sensor_fd = open(FH8626_GC1054_I2C_DEVICE, O_RDWR | O_CLOEXEC);
    if (sensor_fd < 0)
        return -errno;

    if (ioctl(sensor_fd, FH8626_GC1054_I2C_IOCTL_TENBIT, 0) < 0)
        return -errno;
    if (ioctl(sensor_fd, FH8626_GC1054_I2C_IOCTL_FORCE,
              FH8626_GC1054_I2C_FORCE_ARG) < 0)
        return -errno;
    return 0;
}

static int sensor_device_close(void)
{
    if (sensor_fd < 0)
        return 0;
    close(sensor_fd);
    sensor_fd = -1;
    return 0;
}

int Sensor_Write(uint32_t reg, uint32_t value)
{
    uint8_t data[2] = {(uint8_t)reg, (uint8_t)value};
    struct i2c_msg_local msg = {
        .addr = FH8626_GC1054_I2C_MSG_ADDR,
        .flags = 0,
        .len = 2,
        .buf = data,
    };
    struct i2c_rdwr_local rdwr = {&msg, 1};

    if (sensor_fd < 0)
        return -ENODEV;
    if (ioctl(sensor_fd, FH8626_GC1054_I2C_IOCTL_RDWR, &rdwr) < 0)
        return -errno;
    return 0;
}

int Sensor_Read(uint32_t reg)
{
    uint8_t addr = (uint8_t)reg;
    uint8_t value = 0;
    struct i2c_msg_local msgs[2] = {
        {
            .addr = FH8626_GC1054_I2C_MSG_ADDR,
            .flags = 0,
            .len = 1,
            .buf = &addr,
        },
        {
            .addr = FH8626_GC1054_I2C_MSG_ADDR,
            .flags = I2C_M_RD,
            .len = 1,
            .buf = &value,
        },
    };
    struct i2c_rdwr_local rdwr = {msgs, 2};

    if (sensor_fd < 0)
        return -ENODEV;
    if (ioctl(sensor_fd, FH8626_GC1054_I2C_IOCTL_RDWR, &rdwr) < 0)
        return -errno;
    return value;
}

static int gc_init(void)
{
    int words[6];
    int rc;

    memcpy(words, fh8626_gc1054_mipi_init_words, sizeof(words));
    mipi_init(words);
    rc = sensor_device_init();
    if (rc)
        return rc;

    cached_gain = 0x40u;
    cached_intt = 0xd0u;
    current_format = FH8626_GC1054_FORMAT_720P25;
    current_frame_length = 899u;
    return 0;
}

static int gc_set_format(uint32_t format)
{
    const struct fh8626_gc1054_format_contract *f;
    size_t i;
    int rc;

    f = fh8626_gc1054_find_format(format);
    if (!f)
        return -EINVAL;
    if (sensor_fd < 0)
        return -ENODEV;

    for (i = 0; i < FH8626_GC1054_720P25_INIT_COUNT; ++i) {
        struct fh8626_gc1054_reg_write p = fh8626_gc1054_format_pair(f, i);
        if (p.reg == 0xffffu)
            usleep(p.value);
        else {
            rc = Sensor_Write(p.reg, p.value);
            if (rc)
                return rc;
        }
    }

    current_format = f->format;
    current_frame_length = f->frame_length;
    return 0;
}

static int gc_get_vi_attr(void *out)
{
    const struct fh8626_gc1054_format_contract *f;
    struct fh8626_gc1054_vi_attr_raw a;

    if (!out)
        return -EINVAL;
    f = fh8626_gc1054_find_format(current_format);
    if (!f)
        return -EINVAL;
    a = fh8626_gc1054_build_vi_attr(f, orientation_mode != 0);
    memcpy(out, &a, sizeof(a));
    return 0;
}

static int gc_set_gain(uint32_t gain)
{
    struct fh8626_gc1054_gain_program p;
    int rc;

    cached_gain = gain;
    p = fh8626_gc1054_gain_program(gain);

    rc = Sensor_Write(0xfe, 1);
    if (rc)
        return rc;
    if (p.write_triplet) {
        if ((rc = Sensor_Write(0xb6, p.b6)) ||
            (rc = Sensor_Write(0xb1, p.b1)) ||
            (rc = Sensor_Write(0xb2, p.b2)))
            return rc;
    }
    if ((rc = Sensor_Write(0xfe, 4)) ||
        (rc = Sensor_Write(0x40, p.page4_40)) ||
        (rc = Sensor_Write(0xfe, 0)))
        return rc;
    return 0;
}

static int gc_get_gain(uint32_t *gain)
{
    if (!gain)
        return -EINVAL;
    *gain = cached_gain;
    return 0;
}

static int gc_set_intt(uint32_t integration)
{
    uint8_t r03, r04;
    int rc;

    cached_intt = integration;
    fh8626_gc1054_integration_regs(integration, &r03, &r04);
    if ((rc = Sensor_Write(0x03, r03)) ||
        (rc = Sensor_Write(0x04, r04)))
        return rc;
    return 0;
}

static int gc_get_intt(uint32_t *integration)
{
    if (!integration)
        return -EINVAL;
    *integration = cached_intt;
    return 0;
}

static int gc_set_frame_length(uint32_t frame_length)
{
    const struct fh8626_gc1054_format_contract *f;
    uint16_t blank;
    int rc;

    f = fh8626_gc1054_find_format(current_format);
    if (!f || frame_length < 736u)
        return -EINVAL;
    blank = fh8626_gc1054_vblank_from_frame_length(frame_length);
    if ((rc = Sensor_Write(0x07, blank >> 8)) ||
        (rc = Sensor_Write(0x08, blank & 0xffu)))
        return rc;
    current_frame_length = frame_length;
    return 0;
}

static int gc_set_vts_multiplier(uint32_t multiplier)
{
    const struct fh8626_gc1054_format_contract *f;
    uint32_t frame_length;

    f = fh8626_gc1054_find_format(current_format);
    if (!f || !multiplier)
        return -EINVAL;
    frame_length = fh8626_gc1054_format_frame_length_from_multiplier(f, multiplier);
    if (frame_length == current_frame_length)
        return 0;
    return gc_set_frame_length(frame_length);
}

static int gc_write_reg(uint32_t reg, uint32_t value)
{
    return Sensor_Write(reg, value);
}

static int gc_query_max_intt_delta(uint32_t *out)
{
    if (!out)
        return -EINVAL;
    *out = 5;
    return 0;
}

static int gc_control_query(const char *name, uint32_t *out)
{
    const struct fh8626_gc1054_format_contract *f;

    if (!name || !out)
        return -EINVAL;
    f = fh8626_gc1054_find_format(current_format);
    if (!f)
        return -EINVAL;

    if (!strcmp(name, "MAX_INTT_DIFF")) {
        *out = 5;
        return 0;
    }
    if (!strcmp(name, "RGBX")) {
        *out = 0;
        return -EINVAL;
    }
    if (!strcmp(name, "STD_FRAME_RATE") || !strcmp(name, "CUR_FRAME_RATE")) {
        *out = (uint32_t)(f->nominal_fps * 10000.0);
        return 0;
    }
    if (!strcmp(name, "REAL_FLIP_MIRROR")) {
        *out = orientation_mode;
        return 0;
    }
    *out = 0;
    return -EINVAL;
}

static int gc_command(uint32_t command, void *arg)
{
    switch (command) {
    case 1:
        if (!arg)
            return -EINVAL;
        *(uint16_t *)arg = (uint16_t)current_frame_length;
        return (int)current_frame_length;
    case 0x80003:
        if (!arg)
            return -EINVAL;
        orientation_mode = *(uint32_t *)arg;
        return 0;
    default:
        return -ENOSYS;
    }
}

static int gc_close(void)
{
    return sensor_device_close();
}

/*
 * Exact FH8626 0x68-byte callback layout.  Unknown callbacks are intentionally
 * NULL; the Majestic FH8852-shaped facade consumes only the recovered subset.
 */
struct fh8626_gc1054_callbacks {
    const char *name;        /* +00 */
    void *set_gain;          /* +04 */
    void *get_vi_attr;       /* +08 */
    void *get_gain;          /* +0c */
    void *set_intt;          /* +10 */
    void *set_vts;           /* +14 */
    void *get_intt;          /* +18 */
    void *set_mirror_flip;   /* +1c */
    void *get_mirror_flip;   /* +20 */
    void *slot24;            /* +24 */
    void *init;              /* +28 */
    void *slot2c;            /* +2c */
    void *close;             /* +30 */
    void *set_fmt;           /* +34 */
    void *slot38;            /* +38 */
    void *write_reg;         /* +3c */
    void *max_intt_delta;    /* +40 */
    void *slot44;            /* +44 */
    void *slot48;            /* +48 */
    void *control;           /* +4c */
    void *slot50;            /* +50 */
    void *slot54;            /* +54 */
    void *slot58;            /* +58 */
    void *slot5c;            /* +5c */
    void *slot60;            /* +60 */
    void *command;           /* +64 */
};

_Static_assert(sizeof(void *) == 4, "FH8626 sensor ABI requires ARM32");
_Static_assert(sizeof(struct fh8626_gc1054_callbacks) == 0x68,
               "FH8626 GC1054 callback table size");

static struct fh8626_gc1054_callbacks callbacks = {
    .name = "gc1054_mipi",
    .set_gain = gc_set_gain,
    .get_vi_attr = gc_get_vi_attr,
    .get_gain = gc_get_gain,
    .set_intt = gc_set_intt,
    .set_vts = gc_set_vts_multiplier,
    .get_intt = gc_get_intt,
    .init = gc_init,
    .close = gc_close,
    .set_fmt = gc_set_format,
    .write_reg = gc_write_reg,
    .max_intt_delta = gc_query_max_intt_delta,
    .control = gc_control_query,
    .command = gc_command,
};

void *Sensor_Create(void)
{
    return &callbacks;
}

void Sensor_Destroy(void)
{
    (void)sensor_device_close();
}
