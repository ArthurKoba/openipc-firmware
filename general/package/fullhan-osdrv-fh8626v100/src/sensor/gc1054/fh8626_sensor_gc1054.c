#define _GNU_SOURCE
#include "fh8626_sensor_gc1054.h"

#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

/*
 * FH8626 stock sensor plug-in ABI, recovered from libgc1054_mipi.so::Sensor_Create.
 * Sensor_Create returns a pointer to a 0x68-byte callback table.
 *
 * Confirmed callback targets for the stock GC1054 library:
 *   +0x00 -> 0x40b4, "gc1054_mipi"
 *   +0x04 -> 0x18d4, gain programming
 *   +0x08 -> 0x186c, VI attribute getter/wrapper
 *   +0x0c -> 0x148c
 *   +0x10 -> 0x1898, integration/exposure programming (regs 0x03/0x04)
 *   +0x14 -> 0x2064
 *   +0x18 -> 0x14a8
 *   +0x1c -> 0x1d80
 *   +0x20 -> 0x1cac
 *   +0x28 -> 0x1d98, sensor init
 *   +0x2c -> 0x14c4
 *   +0x30 -> 0x1d94, sensor/device close
 *   +0x34 -> 0x2268, set sensor format
 *   +0x3c -> 0x1888, Sensor_Write wrapper
 *   +0x40 -> 0x14c8
 *   +0x4c -> 0x20d8, named control/query interface
 *   +0x64 -> 0x1e68, command/control
 *
 * The ISP copies this table byte-for-byte to global_isp_ctx + 0xc40.
 */
enum {
    CB_NAME       = 0x00,
    CB_SET_GAIN   = 0x04,
    CB_GET_VI     = 0x08,
    CB_GET_0C     = 0x0c,
    CB_SET_INTT   = 0x10,
    CB_UPDATE     = 0x14,
    CB_GET_18     = 0x18,
    CB_1C         = 0x1c,
    CB_20         = 0x20,
    CB_INIT       = 0x28,
    CB_2C         = 0x2c,
    CB_CLOSE      = 0x30,
    CB_SET_FMT    = 0x34,
    CB_KICK       = 0x38,
    CB_WRITE_REG  = 0x3c,
    CB_40         = 0x40,
    CB_CONTROL    = 0x4c,
    CB_COMMAND    = 0x64,
};

static void *ptr_at(const uint8_t *cb, unsigned off)
{
    void *p = NULL;
    if (!cb || off + sizeof(p) > FH_SENSOR_CB_SIZE)
        return NULL;
    memcpy(&p, cb + off, sizeof(p));
    return p;
}

void *fh_sensor_gc1054_cb(const struct fh_sensor_gc1054 *s, unsigned off)
{
    return s ? ptr_at(s->cb, off) : NULL;
}

const char *fh_sensor_gc1054_name(const struct fh_sensor_gc1054 *s)
{
    return (const char *)fh_sensor_gc1054_cb(s, CB_NAME);
}

int fh_sensor_gc1054_open(struct fh_sensor_gc1054 *s, const char *mipi_so, const char *sensor_so)
{
    typedef void *(*sensor_create_fn)(void);
    sensor_create_fn create;

    if (!s || !sensor_so)
        return -EINVAL;
    memset(s, 0, sizeof(*s));

    /* libgc1054_mipi.so has an unresolved mipi_init; load libmipi globally first. */
    if (mipi_so && *mipi_so) {
        s->dl_mipi = dlopen(mipi_so, RTLD_NOW | RTLD_GLOBAL);
        if (!s->dl_mipi) {
            fprintf(stderr, "dlopen(%s): %s\n", mipi_so, dlerror());
            return -ENOENT;
        }
    }

    s->dl_sensor = dlopen(sensor_so, RTLD_NOW | RTLD_GLOBAL);
    if (!s->dl_sensor) {
        fprintf(stderr, "dlopen(%s): %s\n", sensor_so, dlerror());
        fh_sensor_gc1054_close(s);
        return -ENOENT;
    }

    dlerror();
    *(void **)(&create) = dlsym(s->dl_sensor, "Sensor_Create");
    if (!create) {
        fprintf(stderr, "dlsym(Sensor_Create): %s\n", dlerror());
        fh_sensor_gc1054_close(s);
        return -ENOENT;
    }

    s->cb = (uint8_t *)create();
    if (!s->cb) {
        fh_sensor_gc1054_close(s);
        return -EIO;
    }

    if (!fh_sensor_gc1054_cb(s, CB_INIT) || !fh_sensor_gc1054_cb(s, CB_SET_FMT)) {
        fprintf(stderr, "GC1054 callback table is incomplete\n");
        fh_sensor_gc1054_close(s);
        return -EINVAL;
    }
    return 0;
}

void fh_sensor_gc1054_close(struct fh_sensor_gc1054 *s)
{
    if (!s)
        return;
    /* Do not call Sensor_Destory here in the live media owner yet.  The vendor
     * driver teardown lifetime is known to be fragile.  Process-lifetime owner
     * keeps the library loaded. */
    s->cb = NULL;
    if (s->dl_sensor) dlclose(s->dl_sensor);
    if (s->dl_mipi) dlclose(s->dl_mipi);
    memset(s, 0, sizeof(*s));
}

int fh_sensor_gc1054_init(struct fh_sensor_gc1054 *s)
{
    typedef int (*fn_t)(void);
    fn_t fn = NULL;
    *(void **)(&fn) = fh_sensor_gc1054_cb(s, CB_INIT);
    return fn ? fn() : -ENOSYS;
}

int fh_sensor_gc1054_set_fmt(struct fh_sensor_gc1054 *s, uint32_t fmt)
{
    typedef int (*fn_t)(uint32_t);
    fn_t fn = NULL;
    *(void **)(&fn) = fh_sensor_gc1054_cb(s, CB_SET_FMT);
    return fn ? fn(fmt) : -ENOSYS;
}

int fh_sensor_gc1054_set_intt(struct fh_sensor_gc1054 *s, uint32_t intt)
{
    typedef int (*fn_t)(uint32_t);
    fn_t fn = NULL;
    *(void **)(&fn) = fh_sensor_gc1054_cb(s, CB_SET_INTT);
    return fn ? fn(intt) : -ENOSYS;
}

int fh_sensor_gc1054_set_gain(struct fh_sensor_gc1054 *s, uint32_t gain)
{
    typedef int (*fn_t)(uint32_t);
    fn_t fn = NULL;
    *(void **)(&fn) = fh_sensor_gc1054_cb(s, CB_SET_GAIN);
    return fn ? fn(gain) : -ENOSYS;
}

int fh_sensor_gc1054_get_gain(struct fh_sensor_gc1054 *s, uint32_t *gain)
{
    typedef int (*fn_t)(uint32_t *);
    fn_t fn = NULL;
    if (!gain) return -EINVAL;
    *(void **)(&fn) = fh_sensor_gc1054_cb(s, CB_GET_0C);
    return fn ? fn(gain) : -ENOSYS;
}

int fh_sensor_gc1054_get_intt(struct fh_sensor_gc1054 *s, uint32_t *intt)
{
    typedef int (*fn_t)(uint32_t *);
    fn_t fn = NULL;
    if (!intt) return -EINVAL;
    *(void **)(&fn) = fh_sensor_gc1054_cb(s, CB_GET_18);
    return fn ? fn(intt) : -ENOSYS;
}

int fh_sensor_gc1054_set_vts_multiplier(struct fh_sensor_gc1054 *s, uint32_t multiplier)
{
    typedef int (*fn_t)(uint32_t);
    fn_t fn = NULL;
    *(void **)(&fn) = fh_sensor_gc1054_cb(s, CB_UPDATE);
    return fn ? fn(multiplier) : -ENOSYS;
}

void fh_sensor_gc1054_awb_gain(void *opaque,uint32_t gain[3])
{
    typedef void (*fn_t)(uint32_t *);
    fn_t fn=NULL;
    /* CAFC0@CB224 loads sensor table +5c; CB240 supplies module+e0 in r0.
       No claim that a particular sensor library populates this optional slot. */
    *(void **)(&fn)=fh_sensor_gc1054_cb(opaque,0x5cu);
    if(fn)fn(gain);
}

void fh_sensor_gc1054_awb_query(void *opaque,uint32_t gain[3])
{
    typedef void (*fn_t)(uint32_t *);
    fn_t fn=NULL;
    *(void **)(&fn)=fh_sensor_gc1054_cb(opaque,0x58u);
    if(fn)fn(gain);
}

int fh_sensor_gc1054_get_vi_attr(struct fh_sensor_gc1054 *s, void *attr)
{
    typedef int (*fn_t)(void *);
    fn_t fn = NULL;
    *(void **)(&fn) = fh_sensor_gc1054_cb(s, CB_GET_VI);
    return fn ? fn(attr) : -ENOSYS;
}

int fh_sensor_gc1054_write_reg(struct fh_sensor_gc1054 *s, uint32_t reg, uint32_t value)
{
    typedef int (*fn_t)(uint32_t, uint32_t);
    fn_t fn = NULL;
    *(void **)(&fn) = fh_sensor_gc1054_cb(s, CB_WRITE_REG);
    return fn ? fn(reg, value) : -ENOSYS;
}

int fh_sensor_gc1054_read_reg(struct fh_sensor_gc1054 *s, uint32_t reg, uint32_t *value)
{
    typedef int (*fn_t)(uint32_t);
    fn_t fn = NULL;
    int v;
    if (!s || !value || !s->dl_sensor) return -EINVAL;
    dlerror();
    *(void **)(&fn) = dlsym(s->dl_sensor, "Sensor_Read");
    if (!fn) return -ENOSYS;
    v = fn(reg);
    if (v < 0) return v;
    *value = (uint32_t)v;
    return 0;
}

void fh_sensor_gc1054_dump(const struct fh_sensor_gc1054 *s)
{
    static const struct { unsigned off; const char *name; } e[] = {
        {0x00,"name"},{0x04,"set_gain"},{0x08,"get_vi_attr"},{0x0c,"cb0c"},
        {0x10,"set_intt"},{0x14,"update"},{0x18,"cb18"},{0x1c,"cb1c"},
        {0x20,"cb20"},{0x24,"cb24"},{0x28,"init"},{0x2c,"cb2c"},
        {0x30,"close"},{0x34,"set_fmt"},{0x38,"kick"},{0x3c,"write_reg"},
        {0x40,"cb40"},{0x44,"cb44"},{0x48,"cb48"},{0x4c,"control"},
        {0x50,"cb50"},{0x54,"cb54"},{0x58,"cb58"},{0x5c,"cb5c"},
        {0x60,"cb60"},{0x64,"command"},
    };
    size_t i;
    printf("SENSOR name=%s cb=%p\n", fh_sensor_gc1054_name(s) ?: "(null)", s ? (void *)s->cb : NULL);
    for (i = 0; i < sizeof(e)/sizeof(e[0]); ++i)
        printf("  +%02x %-12s %p\n", e[i].off, e[i].name, fh_sensor_gc1054_cb(s, e[i].off));
}
