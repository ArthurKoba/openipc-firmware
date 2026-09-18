#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
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

static const struct library donor_libraries[] = {
    { "/usr/lib/majestic-fh8852v200/libvmm.so" },
    { "/usr/lib/majestic-fh8852v200/libdsp.so" },
    { "/usr/lib/majestic-fh8852v200/libmipi.so" },
    { "/usr/lib/majestic-fh8852v200/libispcore.so" },
    { "/usr/lib/majestic-fh8852v200/libisp.so" },
    { "/usr/lib/majestic-fh8852v200/libadvapi.so" },
    { "/usr/lib/majestic-fh8852v200/libadvapi_isp.so" },
    { "/usr/lib/majestic-fh8852v200/libadvapi_smartir.so" },
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

int main(int argc, char **argv)
{
    enum {
        DONOR_COUNT = sizeof(donor_libraries) / sizeof(donor_libraries[0]),
        COMPAT_COUNT = sizeof(compatibility_libraries) /
                       sizeof(compatibility_libraries[0])
    };
    void *handles[COMPAT_COUNT > DONOR_COUNT ? COMPAT_COUNT : DONOR_COUNT];
    const struct library *set;
    size_t count;
    const char *title;
    int devices_missing;
    int symbols_missing;

    memset(handles, 0, sizeof(handles));
    puts("FH8626V100 Majestic compatibility ABI probe");
    devices_missing = probe_devices();

    if (argc != 2 ||
        (strcmp(argv[1], "--donor") && strcmp(argv[1], "--compat"))) {
        fprintf(stderr, "usage: %s {--donor|--compat}\n", argv[0]);
        return 64;
    }

    if (!strcmp(argv[1], "--donor")) {
        set = donor_libraries;
        count = DONOR_COUNT;
        title = "donor-baseline-libraries:";
    } else {
        set = compatibility_libraries;
        count = COMPAT_COUNT;
        title = "fh8626-source-compatibility-libraries:";
    }

    if (load_set(title, set, count, handles)) {
        close_set(handles, count);
        fprintf(stderr, "probe result: selected dependency closure is not loadable\n");
        return 2;
    }

    symbols_missing = probe_symbols();
    close_set(handles, count);

    printf("probe result: mode=%s devices_missing=%d required_symbols_missing=%d\n",
           argv[1], devices_missing, symbols_missing);

    return symbols_missing ? 3 : 0;
}
