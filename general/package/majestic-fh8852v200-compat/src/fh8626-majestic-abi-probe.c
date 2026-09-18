#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

struct library {
    const char *path;
};

struct symbol {
    const char *name;
    const char *layer;
    int required;
};


static const char *devices[] = {
    "/dev/vmm_userdev",
    "/dev/media_process",
    "/dev/isp",
    "/dev/pae",
    "/dev/jpeg",
};

static const struct symbol symbols[] = {
    { "FH_SYS_VmmAlloc", "vmm", 1 },
    { "FH_SYS_VmmFree", "vmm", 1 },
    { "FH_SYS_Mmap", "vmm", 1 },
    { "FH_SYS_Munmap", "vmm", 1 },
    { "FH_SYS_GetVRegAddr", "vmm", 0 },
    { "FH_SYS_VmmGetPaddr_ByVaddr", "vmm", 0 },

    { "FH_SYS_Init", "system", 1 },
    { "FH_SYS_Exit", "system", 1 },
    { "FH_SYS_Set_Resource", "system", 0 },
    { "FH_SYS_BindVpu2Enc", "system", 1 },

    { "FH_VPSS_SysInitMem", "vi/vpss", 1 },
    { "FH_VPSS_SetViAttr", "vi/vpss", 1 },
    { "FH_VPSS_QuerySysMem", "vi/vpss", 1 },
    { "FH_VPSS_QueryChnMem", "vi/vpss", 1 },
    { "FH_VPSS_ChnInitMem", "vi/vpss", 1 },
    { "FH_VPSS_OpenChn", "vi/vpss", 1 },
    { "FH_VPSS_CloseChn", "vi/vpss", 1 },
    { "FH_VPSS_Enable", "vi/vpss", 1 },
    { "FH_VPSS_Disable", "vi/vpss", 1 },
    { "FH_VPSS_SetFramectrl", "vi/vpss", 0 },

    { "FH_VPSS_GetViAttr", "vi/vpss", 1 },
    { "FH_VPSS_GetChnAttr", "vi/vpss", 1 },
    { "FH_VPSS_EnableYCmean", "motion/osd", 1 },
    { "FH_VPSS_DisableYCmean", "motion/osd", 1 },
    { "FH_VPSS_GetYCmean", "motion/osd", 1 },
    { "FH_VPSS_GetCPYData", "motion", 1 },
    { "FH_VPSS_SetChnGraphV2", "osd", 1 },
    { "FH_VPSS_GetChnGraphV2", "osd", 1 },
    { "FH_VPSS_SetGlbGraphV2", "osd", 1 },
    { "FH_VPSS_GetGlbGraphV2", "osd", 1 },

    { "FH_VENC_SysInitMem", "venc", 1 },
    { "FH_VENC_CreateChn", "venc", 1 },
    { "FH_VENC_SetChnAttr", "venc", 1 },
    { "FH_VENC_StartRecvPic", "venc", 1 },
    { "FH_VENC_StopRecvPic", "venc", 1 },
    { "FH_VENC_GetStream", "venc", 1 },
    { "FH_VENC_GetStream_Block", "venc", 0 },
    { "FH_VENC_ReleaseStream", "venc", 1 },
    { "FH_VENC_RequestIDR", "venc", 1 },
    { "FH_VENC_SetRCAttr", "venc", 0 },
    { "FH_VENC_SetRcChangeParam", "venc", 0 },

    { "FH_VENC_GetRCAttr", "venc", 1 },
    { "FH_VENC_GetChnAttr", "venc", 1 },

    { "_JPEG_SysInit", "jpeg", 1 },
    { "_JPEG_QueryChnMem", "jpeg", 1 },
    { "_JPEG_CreateChn", "jpeg", 1 },
    { "_JPEG_DestroyChn", "jpeg", 1 },
    { "_JPEG_SetChnAttr", "jpeg", 1 },
    { "_JPEG_GetChnAttr", "jpeg", 1 },
    { "_JPEG_SetRCAttr", "jpeg", 1 },
    { "_JPEG_GetRCAttr", "jpeg", 1 },
    { "_JPEG_Start", "jpeg", 1 },
    { "_JPEG_Stop", "jpeg", 1 },
    { "_JPEG_HandleStream", "jpeg", 1 },
    { "_JPEG_ReleaseStream", "jpeg", 1 },

    { "FH_AC_Init", "audio", 1 },
    { "FH_AC_DeInit", "audio", 1 },
    { "FH_AC_Set_Config", "audio", 1 },
    { "FH_AC_AI_Enable", "audio", 1 },
    { "FH_AC_AI_Disable", "audio", 1 },
    { "FH_AC_AI_GetFrameWithPtsFast", "audio", 1 },
    { "FH_AC_AO_Enable", "audio", 1 },
    { "FH_AC_AO_Disable", "audio", 1 },
    { "FH_AC_AO_SendFrame", "audio", 1 },
    { "FH_AC_AEC_SetConfig", "audio-vqe", 1 },
    { "FH_AC_NR_SetConfig", "audio-vqe", 1 },
    { "FH_AC_Agc_SetConfig", "audio-vqe", 1 },
    { "FH_AC_Ext2_Ioctl", "audio-vqe", 1 },

    { "mipi_init", "sensor/mipi", 1 },
    { "API_ISP_SensorRegCb", "sensor/isp", 1 },
    { "API_ISP_SensorUnRegCb", "sensor/isp", 1 },
    { "API_ISP_SensorInit", "sensor/isp", 1 },
    { "API_ISP_SetSensorFmt", "sensor/isp", 1 },

    { "API_ISP_MemInit", "isp", 1 },
    { "API_ISP_Init", "isp", 1 },
    { "API_ISP_Run", "isp", 1 },
    { "API_ISP_Exit", "isp", 1 },
    { "API_ISP_GetAeStatus", "isp", 0 },
    { "API_ISP_SetAwbGain", "isp", 0 },
    { "API_ISP_SetMirrorAndflip", "isp", 0 },

    { "FHAdv_Isp_SensorInit", "adv-isp", 1 },
    { "FHAdv_Isp_Init", "adv-isp", 1 },
    { "FHAdv_Isp_SetAEMode", "adv-isp", 0 },
    { "FHAdv_Isp_SetColorMode", "adv-isp", 0 },
};

static int probe_devices(void)
{
    size_t i;
    int missing = 0;

    puts("native-devices:");
    for (i = 0; i < sizeof(devices) / sizeof(devices[0]); ++i) {
        int ok = access(devices[i], R_OK | W_OK) == 0;
        printf("  %-22s %s\n", devices[i], ok ? "ready" : "missing");
        if (!ok)
            missing++;
    }
    return missing;
}

#define COMPAT_SENSOR_FACADE_INDEX 5u

static const struct library compatibility_libraries[] = {
    { "/usr/lib/majestic-fh8626/libvmm.so" },
    { "/usr/lib/majestic-fh8626/libdsp.so" },
    { "/usr/lib/majestic-fh8626/libmipi.so" },
    { "/usr/lib/majestic-fh8626/libacw_mpi.so" },
    { "/usr/lib/majestic-fh8626/libgc1054_fh8626_native.so" },
    { "/usr/lib/majestic-fh8626/libgc1054_mipi.so" },
    { "/usr/lib/majestic-fh8852v200/libispcore.so" },
    { "/usr/lib/majestic-fh8852v200/libisp.so" },
    { "/usr/lib/majestic-fh8852v200/libadvapi.so" },
    { "/usr/lib/majestic-fh8852v200/libadvapi_isp.so" },
    { "/usr/lib/majestic-fh8852v200/libadvapi_md.so" },
    { "/usr/lib/majestic-fh8852v200/libadvapi_osd.so" },
    { "/usr/lib/majestic-fh8852v200/libadvapi_smartir.so" },
};

static int load_set(const char *title, const struct library *libraries,
                    size_t count, void **handles)
{
    size_t i;

    puts(title);
    for (i = 0; i < count; ++i) {
        handles[i] = dlopen(libraries[i].path, RTLD_NOW | RTLD_GLOBAL);
        if (!handles[i]) {
            printf("  %-52s FAIL: %s\n", libraries[i].path, dlerror());
            return -1;
        }
        printf("  %-52s loaded\n", libraries[i].path);
    }
    return 0;
}

static void close_set(void **handles, size_t count)
{
    size_t i;
    for (i = count; i > 0; --i) {
        if (handles[i - 1])
            dlclose(handles[i - 1]);
    }
}

static int probe_symbols(void)
{
    size_t i;
    int required_missing = 0;

    puts("majestic-fullhan-abi:");
    for (i = 0; i < sizeof(symbols) / sizeof(symbols[0]); ++i) {
        void *address;
        const char *error;

        dlerror();
        address = dlsym(RTLD_DEFAULT, symbols[i].name);
        error = dlerror();
        printf("  %-11s %-32s %s%s\n",
               symbols[i].layer,
               symbols[i].name,
               error ? "missing" : "resolved",
               symbols[i].required ? " [required]" : "");
        if (symbols[i].required && (!address || error))
            required_missing++;
    }
    return required_missing;
}

struct sensor_slot {
    uint32_t offset;
    const char *name;
};

static const struct sensor_slot required_sensor_slots[] = {
    { 0x00u, "name" },
    { 0x04u, "get_vi_attr" },
    { 0x08u, "set_flip_mirror" },
    { 0x0cu, "get_flip_mirror" },
    { 0x10u, "set_iris" },
    { 0x14u, "init" },
    { 0x18u, "reset" },
    { 0x1cu, "deinit" },
    { 0x20u, "set_fmt" },
    { 0x24u, "kick" },
    { 0x28u, "set_reg" },
    { 0x2cu, "set_exposure_ratio" },
    { 0x30u, "get_exposure_ratio" },
    { 0x34u, "get_sensor_attribute" },
    { 0x38u, "set_lane_num_max" },
    { 0x3cu, "get_reg" },
    { 0x40u, "get_awb_gain" },
    { 0x44u, "set_awb_gain" },
    /* +0x48 is reserved and intentionally NULL. */
    { 0x4cu, "common_if" },
    { 0x50u, "get_ae_default" },
    { 0x54u, "get_ae_info" },
    { 0x58u, "set_intt" },
    { 0x5cu, "calc_valid_intt" },
    { 0x60u, "set_gain" },
    { 0x64u, "calc_valid_gain" },
    { 0x68u, "set_frame_h" },
    { 0x6cu, "get_mirror_bayer" },
    { 0x70u, "get_user_awb_gain" },
    { 0x74u, "get_ltm_curve" },
    { 0x78u, "is_connect" },
};

static int probe_sensor_table(void *facade_handle)
{
    typedef void *(*create_fn)(void);
    typedef void (*destroy_fn)(void);
    create_fn create = NULL;
    destroy_fn destroy = NULL;
    const uint8_t *table;
    size_t i;
    int missing = 0;

    if (!facade_handle) {
        puts("sensor-callbacks: facade handle missing");
        return 1;
    }

    dlerror();
    *(void **)(&create) = dlsym(facade_handle, "Sensor_Create");
    if (!create || dlerror()) {
        puts("sensor-callbacks: Sensor_Create missing");
        return 1;
    }

    table = create();
    if (!table) {
        puts("sensor-callbacks: Sensor_Create returned NULL");
        return 1;
    }

    puts("fh8852-sensor-callbacks:");
    for (i = 0; i < sizeof(required_sensor_slots) / sizeof(required_sensor_slots[0]); ++i) {
        void *value = NULL;
        memcpy(&value, table + required_sensor_slots[i].offset, sizeof(value));
        printf("  +0x%02x %-24s %s\n",
               required_sensor_slots[i].offset,
               required_sensor_slots[i].name,
               value ? "ready" : "missing");
        if (!value)
            missing++;
    }

    dlerror();
    *(void **)(&destroy) = dlsym(facade_handle, "Sensor_Destroy");
    if (destroy && !dlerror())
        destroy();

    return missing;
}

int main(int argc, char **argv)
{
    enum {
        COMPAT_COUNT = sizeof(compatibility_libraries) /
                       sizeof(compatibility_libraries[0])
    };
    void *handles[COMPAT_COUNT];
    int devices_missing;
    int symbols_missing;
    int sensor_missing;

    memset(handles, 0, sizeof(handles));
    puts("FH8626V100 Majestic selected-runtime ABI probe");
    devices_missing = probe_devices();

    if (argc != 2 || strcmp(argv[1], "--compat")) {
        fprintf(stderr, "usage: %s --compat\n", argv[0]);
        return 64;
    }

    if (load_set("selected-runtime-libraries:",
                 compatibility_libraries, COMPAT_COUNT, handles)) {
        close_set(handles, COMPAT_COUNT);
        fprintf(stderr, "probe result: selected dependency closure is not loadable\n");
        return 2;
    }

    sensor_missing = probe_sensor_table(handles[COMPAT_SENSOR_FACADE_INDEX]);
    symbols_missing = probe_symbols();
    close_set(handles, COMPAT_COUNT);

    printf("probe result: mode=compat devices_missing=%d required_symbols_missing=%d sensor_callbacks_missing=%d\n",
           devices_missing, symbols_missing, sensor_missing);

    if (symbols_missing)
        return 3;
    if (devices_missing)
        return 4;
    if (sensor_missing)
        return 5;
    return 0;
}
