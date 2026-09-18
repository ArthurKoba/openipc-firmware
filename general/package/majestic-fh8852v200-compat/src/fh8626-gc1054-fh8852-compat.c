#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * FH8852V200 sensor plug-in facade for the FH8626V100 GC1054 bring-up path.
 *
 * This is compatibility staging, not a production sensor backend. FH8852V200
 * sensor plug-ins expose a 0x7c-byte callback table while the recovered
 * FH8626V100 GC1054 plug-in exposes a different 0x68-byte table. Passing the
 * FH8626 table directly to the FH8852 ISP corrupts the ABI boundary.
 *
 * The facade translates the selected FH8852 callback ABI onto recovered
 * FH8626 GC1054 state/operations. The current callback table contains no
 * permissive unresolved stubs; optional donor semantics are implemented only
 * where stock FH8626 or repeated FH8852 donor evidence establishes behavior.
 */

#define FH8626_CB_SIZE 0x68u
#define FH8852_CB_SIZE 0x7cu

enum {
    FH8626_NAME       = 0x00,
    FH8626_SET_GAIN   = 0x04,
    FH8626_GET_VI     = 0x08,
    FH8626_GET_GAIN   = 0x0c,
    FH8626_SET_INTT   = 0x10,
    FH8626_UPDATE     = 0x14,
    FH8626_GET_INTT   = 0x18,
    FH8626_SET_MIRROR = 0x1c,
    FH8626_GET_MIRROR = 0x20,
    FH8626_INIT       = 0x28,
    FH8626_CLOSE      = 0x30,
    FH8626_SET_FMT    = 0x34,
    FH8626_KICK       = 0x38,
    FH8626_WRITE_REG  = 0x3c,
    FH8626_MAX_INTT_DELTA = 0x40,
    FH8626_CONTROL    = 0x4c,
    FH8626_AWB_QUERY  = 0x58,
    FH8626_AWB_SET    = 0x5c,
    FH8626_COMMAND    = 0x64,
};

struct fh8852_sensor_if {
    const char *name;                                      /* +0x00 */
    int (*get_vi_attr)(void *);                            /* +0x04 */
    int (*set_flip_mirror)(uint32_t);                      /* +0x08 */
    int (*get_flip_mirror)(uint32_t *);                    /* +0x0c */
    int (*set_iris)(uint32_t);                          /* +0x10 */
    int (*init)(void);                                     /* +0x14 */
    int (*reset)(void);                                 /* +0x18 */
    int (*deinit)(void);                                   /* +0x1c */
    int (*set_fmt)(uint32_t);                              /* +0x20 */
    int (*kick)(void);                                     /* +0x24 */
    int (*set_reg)(uint16_t, uint16_t);                    /* +0x28 */
    int (*set_exposure_ratio)(uint32_t);                   /* +0x2c */
    int (*get_exposure_ratio)(uint32_t *);                 /* +0x30 */
    int (*get_sensor_attribute)(const char *, uint32_t *); /* +0x34 */
    int (*set_lane_num_max)(uint32_t);                     /* +0x38 */
    int (*get_reg)(uint16_t, uint16_t *);                  /* +0x3c */
    int (*get_awb_gain)(uint32_t *);                       /* +0x40 */
    int (*set_awb_gain)(uint32_t *);                       /* +0x44 */
    void *reserved_48;                                     /* +0x48 */
    int (*common_if)(uint32_t, void *, uint32_t);          /* +0x4c */
    int (*get_ae_default)(uint32_t *);                     /* +0x50 */
    int (*get_ae_info)(uint32_t *);                        /* +0x54 */
    int (*set_intt)(uint32_t, uint32_t);                   /* +0x58 */
    int (*calc_valid_intt)(uint32_t *);                    /* +0x5c */
    int (*set_gain)(uint32_t, uint32_t);                   /* +0x60 */
    int (*calc_valid_gain)(uint32_t *);                    /* +0x64 */
    int (*set_frame_h)(uint32_t);                          /* +0x68 */
    const uint32_t *(*get_mirror_bayer)(void);             /* +0x6c */
    const uint32_t *(*get_user_awb_gain)(uint32_t);        /* +0x70 */
    const void *(*get_ltm_curve)(uint32_t);                /* +0x74 */
    int (*is_connect)(void);                               /* +0x78 */
};

_Static_assert(sizeof(void *) == 4, "FH8852 sensor ABI requires 32-bit ARM pointers");
_Static_assert(sizeof(struct fh8852_sensor_if) == FH8852_CB_SIZE,
               "FH8852 sensor callback table must be exactly 0x7c bytes");

static void *native_handle;
static uint8_t *native_if;
static uint32_t awb_gain[3];
static uint32_t exposure_ratio = 0x100u;

static void *native_cb(unsigned off)
{
    void *fn = NULL;

    if (!native_if || off + sizeof(fn) > FH8626_CB_SIZE)
        return NULL;
    memcpy(&fn, native_if + off, sizeof(fn));
    return fn;
}

static int native_open(void)
{
    typedef void *(*create_fn)(void);
    const char *path;
    create_fn create = NULL;

    if (native_if)
        return 0;

    path = getenv("FH8626_NATIVE_SENSOR_SO");
    if (!path || !path[0])
        path = "/usr/lib/majestic-fh8626/libgc1054_fh8626_native.so";

    if (!native_handle) {
        native_handle = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
        if (!native_handle) {
            fprintf(stderr, "fh8626-majestic-sensor: dlopen(%s): %s\n",
                    path, dlerror());
            return -ENOENT;
        }
    }

    dlerror();
    *(void **)(&create) = dlsym(native_handle, "Sensor_Create");
    if (!create) {
        fprintf(stderr, "fh8626-majestic-sensor: Sensor_Create: %s\n",
                dlerror());
        return -ENOENT;
    }

    native_if = create();
    if (!native_if)
        return -EIO;
    return 0;
}

static int call0(unsigned off)
{
    typedef int (*fn_t)(void);
    fn_t fn = NULL;

    if (native_open())
        return -EIO;
    *(void **)(&fn) = native_cb(off);
    return fn ? fn() : -ENOSYS;
}

static int call1(unsigned off, uintptr_t a0)
{
    typedef int (*fn_t)(uintptr_t);
    fn_t fn = NULL;

    if (native_open())
        return -EIO;
    *(void **)(&fn) = native_cb(off);
    return fn ? fn(a0) : -ENOSYS;
}

static int call2(unsigned off, uintptr_t a0, uintptr_t a1)
{
    typedef int (*fn_t)(uintptr_t, uintptr_t);
    fn_t fn = NULL;

    if (native_open())
        return -EIO;
    *(void **)(&fn) = native_cb(off);
    return fn ? fn(a0, a1) : -ENOSYS;
}

static int native_control_query(const char *name, uint32_t *value)
{
    typedef int (*fn_t)(const char *, uint32_t *);
    fn_t fn = NULL;

    if (!name || !value)
        return -EINVAL;
    if (native_open())
        return -EIO;
    *(void **)(&fn) = native_cb(FH8626_CONTROL);
    return fn ? fn(name, value) : -ENOSYS;
}

static int native_frame_length(uint32_t *frame_length)
{
    typedef int (*fn_t)(uint32_t, void *);
    fn_t fn = NULL;
    uint16_t value = 0;
    int rc;

    if (!frame_length)
        return -EINVAL;
    if (native_open())
        return -EIO;
    *(void **)(&fn) = native_cb(FH8626_COMMAND);
    if (!fn)
        return -ENOSYS;
    rc = fn(1u, &value);
    if (rc < 0)
        return rc;
    *frame_length = value;
    return 0;
}

static int native_base_frame_length(uint32_t *frame_length)
{
    typedef int (*fn_t)(uint32_t *);
    fn_t fn = NULL;

    if (!frame_length)
        return -EINVAL;
    if (native_open())
        return -EIO;
    *(void **)(&fn) = dlsym(native_handle, "Sensor_GetBaseFrameLength");
    return fn ? fn(frame_length) : -ENOSYS;
}

static int native_get_u32(unsigned off, uint32_t *value)
{
    typedef int (*fn_t)(uint32_t *);
    fn_t fn = NULL;

    if (!value)
        return -EINVAL;
    if (native_open())
        return -EIO;
    *(void **)(&fn) = native_cb(off);
    return fn ? fn(value) : -ENOSYS;
}

static int compat_get_vi_attr(void *attr)
{
    return call1(FH8626_GET_VI, (uintptr_t)attr);
}

static int compat_set_flip_mirror(uint32_t value)
{
    return call1(FH8626_SET_MIRROR, value);
}

static int compat_get_flip_mirror(uint32_t *value)
{
    typedef int (*fn_t)(uint32_t *);
    fn_t fn = NULL;
    int rc;

    if (!value)
        return -EINVAL;
    if (native_open())
        return -EIO;
    *(void **)(&fn) = native_cb(FH8626_GET_MIRROR);
    if (!fn)
        return -ENOSYS;
    rc = fn(value);
    return rc;
}

static int compat_set_iris(uint32_t value)
{
    (void)value;
    /* All three audited FH8852 donor sensors implement this as success/no-op. */
    return 0;
}

int Sensor_Init(void)
{
    return call0(FH8626_INIT);
}

static int compat_reset(void)
{
    /* The audited FH8852 reset callbacks are empty; GPIO5 remains board-owned. */
    return 0;
}

int Sensor_DeInit(void)
{
    int rc = call0(FH8626_CLOSE);

    return rc == -ENOSYS ? 0 : rc;
}

static int compat_set_fmt(uint32_t fmt)
{
    return call1(FH8626_SET_FMT, fmt);
}

static int compat_kick(void)
{
    int rc = call0(FH8626_KICK);
    return rc == -ENOSYS ? 0 : rc;
}

int Sensor_Write(uint32_t reg, uint32_t value)
{
    return call2(FH8626_WRITE_REG, reg, value);
}

int Sensor_Read(uint32_t reg)
{
    typedef int (*fn_t)(uint32_t);
    fn_t fn = NULL;

    if (native_open())
        return -EIO;
    *(void **)(&fn) = dlsym(native_handle, "Sensor_Read");
    return fn ? fn(reg) : -ENOSYS;
}

static int compat_set_exposure_ratio(uint32_t value)
{
    exposure_ratio = value;
    return 0;
}

static int compat_get_exposure_ratio(uint32_t *value)
{
    if (!value)
        return -EINVAL;
    /*
     * The selected GC1054 path is linear (non-WDR). Retain the public ratio
     * state set by FH8852 rather than inventing a short-exposure channel.
     */
    *value = exposure_ratio;
    return 0;
}

static int compat_get_sensor_attribute(const char *name, uint32_t *value)
{
    if (!name || !value)
        return -EINVAL;
    if (!strcmp(name, "WDR")) {
        *value = 0;
        return 0;
    }
    return -1;
}

static int compat_set_lane_num_max(uint32_t lanes)
{
    (void)lanes;
    /*
     * FH8626 stock GC1054 has no lane-max callback. Its hardware-proven MIPI
     * lane/PHY contract is supplied by the fixed native GC1054 init words.
     */
    return 0;
}

static int compat_set_reg(uint16_t reg, uint16_t value)
{
    return Sensor_Write(reg, value);
}

static int compat_get_reg(uint16_t reg, uint16_t *value)
{
    int rc;

    if (!value)
        return -EINVAL;
    rc = Sensor_Read(reg);
    if (rc < 0)
        return rc;
    *value = (uint16_t)rc;
    return 0;
}

static int compat_get_awb_gain(uint32_t gain[3])
{
    typedef void (*fn_t)(uint32_t *);
    fn_t fn = NULL;

    if (!gain)
        return -EINVAL;
    if (!native_open()) {
        *(void **)(&fn) = native_cb(FH8626_AWB_QUERY);
        if (fn)
            fn(gain);
        else
            memcpy(gain, awb_gain, sizeof(awb_gain));
    }
    return 0;
}

static int compat_set_awb_gain(uint32_t gain[3])
{
    typedef void (*fn_t)(uint32_t *);
    fn_t fn = NULL;

    if (!gain)
        return -EINVAL;
    memcpy(awb_gain, gain, sizeof(awb_gain));
    if (!native_open()) {
        *(void **)(&fn) = native_cb(FH8626_AWB_SET);
        if (fn)
            fn(gain);
    }
    return 0;
}

static int compat_common_if(uint32_t command, void *arg, uint32_t reserved)
{
    (void)command;
    (void)arg;
    (void)reserved;
    /*
     * GC4653 and JXF32 FH8852 donors return -1 for this optional interface.
     * Do not pass integer FH8852 commands into the unrelated FH8626 string
     * control-query callback.
     */
    return -1;
}

static int compat_get_ae_default(uint32_t value[6])
{
    uint32_t base_frame_length;
    uint32_t margin = 5u;
    int rc;

    if (!value)
        return -EINVAL;
    rc = native_base_frame_length(&base_frame_length);
    if (rc)
        return rc;
    if (native_get_u32(FH8626_MAX_INTT_DELTA, &margin))
        margin = 5u;

    value[0] = 1u; /* selected linear GC1054 path */
    value[1] = base_frame_length > margin ? base_frame_length - margin : 1u;
    value[2] = 0x40u; /* donor sns_cfg default gain, not current runtime gain */
    value[3] = 0u; /* donor-specific max-gain hint is not consumed by libispcore */
    value[4] = base_frame_length;
    value[5] = margin;
    return 0;
}

static int compat_get_ae_info(uint32_t value[4])
{
    uint32_t base_frame_length;
    uint32_t current_frame_length;
    uint32_t fps10000 = 250000u;
    int rc;

    if (!value)
        return -EINVAL;
    if ((rc = native_get_u32(FH8626_GET_INTT, &value[0])))
        return rc;
    if ((rc = native_get_u32(FH8626_GET_GAIN, &value[1])))
        return rc;
    if ((rc = native_base_frame_length(&base_frame_length)))
        return rc;
    if ((rc = native_frame_length(&current_frame_length)))
        return rc;
    if (native_control_query("CUR_FRAME_RATE", &fps10000))
        fps10000 = 250000u;

    value[2] = base_frame_length * ((fps10000 + 5000u) / 10000u);
    value[3] = current_frame_length;
    return 0;
}

static int compat_set_intt(uint32_t value, uint32_t exposure_index)
{
    if (exposure_index != 0u)
        return 0;
    return call1(FH8626_SET_INTT, value);
}

static int compat_calc_valid_intt(uint32_t *value)
{
    uint32_t frame_length;
    uint32_t margin = 5u;
    uint32_t maximum;
    int rc;

    if (!value)
        return -EINVAL;
    if ((rc = native_frame_length(&frame_length)))
        return rc;
    if (native_get_u32(FH8626_MAX_INTT_DELTA, &margin))
        margin = 5u;
    maximum = frame_length > margin ? frame_length - margin : 1u;
    if (*value == 0u)
        *value = 1u;
    if (*value > maximum)
        *value = maximum;
    return 0;
}

static int compat_set_gain(uint32_t value, uint32_t exposure_index)
{
    if (exposure_index != 0u)
        return 0;
    return call1(FH8626_SET_GAIN, value);
}

static int compat_calc_valid_gain(uint32_t *value)
{
    if (!value)
        return -EINVAL;
    /*
     * Stock GC1054 SetGain performs its own quantization. The audited FH8852
     * GC4653/MN34425 valid-gain callbacks likewise accept the requested value.
     */
    if (*value < 0x40u)
        *value = 0x40u;
    return 0;
}

static int compat_set_frame_h(uint32_t value)
{
    typedef int (*fn_t)(uint32_t);
    fn_t fn = NULL;

    if (native_open())
        return -EIO;
    *(void **)(&fn) = dlsym(native_handle, "Sensor_SetFrameLength");
    return fn ? fn(value) : -ENOSYS;
}

static const uint32_t *compat_get_mirror_bayer(void)
{
    typedef const uint32_t *(*fn_t)(void);
    fn_t fn = NULL;

    if (native_open())
        return NULL;
    *(void **)(&fn) = dlsym(native_handle, "GetMirrorFlipBayerFormat");
    return fn ? fn() : NULL;
}

static const uint32_t *compat_get_user_awb_gain(uint32_t index)
{
    (void)index;
    /* Stock FH8626 GC1054 exposes no user-AWB preset table. */
    return NULL;
}

static const void *compat_get_ltm_curve(uint32_t index)
{
    (void)index;
    /* Stock FH8626 GC1054 returns NULL for this optional callback. */
    return NULL;
}

int Sensor_Isconnect(void)
{
    typedef int (*fn_t)(void);
    fn_t fn = NULL;

    if (native_open())
        return 0;
    *(void **)(&fn) = dlsym(native_handle, "Sensor_Isconnect");
    return fn ? fn() : 0;
}

static struct fh8852_sensor_if compat_if = {
    .name = "gc1054_mipi",
    .get_vi_attr = compat_get_vi_attr,
    .set_flip_mirror = compat_set_flip_mirror,
    .get_flip_mirror = compat_get_flip_mirror,
    .set_iris = compat_set_iris,
    .init = Sensor_Init,
    .reset = compat_reset,
    .deinit = Sensor_DeInit,
    .set_fmt = compat_set_fmt,
    .kick = compat_kick,
    .set_reg = compat_set_reg,
    .set_exposure_ratio = compat_set_exposure_ratio,
    .get_exposure_ratio = compat_get_exposure_ratio,
    .get_sensor_attribute = compat_get_sensor_attribute,
    .set_lane_num_max = compat_set_lane_num_max,
    .get_reg = compat_get_reg,
    .get_awb_gain = compat_get_awb_gain,
    .set_awb_gain = compat_set_awb_gain,
    .reserved_48 = NULL,
    .common_if = compat_common_if,
    .get_ae_default = compat_get_ae_default,
    .get_ae_info = compat_get_ae_info,
    .set_intt = compat_set_intt,
    .calc_valid_intt = compat_calc_valid_intt,
    .set_gain = compat_set_gain,
    .calc_valid_gain = compat_calc_valid_gain,
    .set_frame_h = compat_set_frame_h,
    .get_mirror_bayer = compat_get_mirror_bayer,
    .get_user_awb_gain = compat_get_user_awb_gain,
    .get_ltm_curve = compat_get_ltm_curve,
    .is_connect = Sensor_Isconnect,
};

void *Sensor_Create(void)
{
    if (native_open())
        return NULL;
    return &compat_if;
}

void Sensor_Destroy(void)
{
    typedef void (*destroy_fn)(void);
    destroy_fn destroy = NULL;

    if (native_handle) {
        dlerror();
        *(void **)(&destroy) = dlsym(native_handle, "Sensor_Destroy");
        if (destroy)
            destroy();
    }

    /*
     * Keep the dlopen handle for same-process re-create. Dropping our pointer
     * without dlclose leaked a reference on every reload; dlclosing here would
     * also discard libmipi's mapping bookkeeping while its MMIO mappings remain
     * process-owned. Sensor_Create() will reuse this handle and rebuild native_if.
     */
    native_if = NULL;
    exposure_ratio = 0x100u;
    memset(awb_gain, 0, sizeof(awb_gain));
}
