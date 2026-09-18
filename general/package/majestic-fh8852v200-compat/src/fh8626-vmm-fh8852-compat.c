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
 * FH8852V200 libvmm facade for FH8626V100 staging.
 *
 * FH8852 allocates through ioctl 0xC0506D0A (0x50-byte wire), while FH8626
 * uses 0xC0686D0A (0x68-byte wire).  Do not pass the donor allocation record
 * to the FH8626 driver.  The donor consumers in the retained Majestic closure
 * import only buffer_malloc_withname{,_cached}; the remaining public FH_SYS_*
 * names are exported as explicit diagnostics so a direct Majestic dependency
 * fails cleanly instead of silently reaching the wrong ABI.
 */

#define FH8626_VMM_ALLOC 0xC0686D0AUL

struct fh_vmm_buffer {
    uint32_t phys;
    uint32_t virt;
    uint32_t size;
};

static int vmm_fd = -1;

static int trace_enabled(void)
{
    const char *v = getenv("FH8626_MAJESTIC_TRACE");
    return v && v[0] && strcmp(v, "0");
}

static int open_vmm(void)
{
    if (vmm_fd >= 0)
        return 0;
    vmm_fd = open("/dev/vmm_userdev", O_RDWR | O_CLOEXEC);
    return vmm_fd >= 0 ? 0 : -errno;
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

static int alloc_native(struct fh_vmm_buffer *out, uint32_t size,
                        const char *name)
{
    uint8_t request[104];
    void *mapped;
    int rc;

    if (!out || !size)
        return -EINVAL;
    rc = open_vmm();
    if (rc)
        return rc;

    memset(out, 0, sizeof(*out));
    size = (size + 4095u) & ~4095u;
    memset(request, 0, sizeof(request));
    memcpy(request + 8, &(uint32_t){4096u}, 4);
    memcpy(request + 12, &size, 4);
    strncpy((char *)request + 28, name && name[0] ? name : "majestic-vmm", 15);
    strncpy((char *)request + 44, "anonymous", 15);

    errno = 0;
    rc = ioctl(vmm_fd, FH8626_VMM_ALLOC, request);
    if (rc)
        return rc == -1 && errno ? -errno : -EIO;

    memcpy(&out->phys, request, 4);
    out->size = size;
    mapped = map_phys(out->phys, size);
    if (mapped == MAP_FAILED) {
        memset(out, 0, sizeof(*out));
        return -errno;
    }
    memset(mapped, 0, size);
    out->virt = (uint32_t)(uintptr_t)mapped;

    if (trace_enabled())
        fprintf(stderr,
                "fh8626-vmm-compat: alloc name=%s size=%u phys=%08x virt=%08x\n",
                name ? name : "(null)", size, out->phys, out->virt);
    return 0;
}

int buffer_malloc_withname(struct fh_vmm_buffer *out, uint32_t size,
                           uint32_t flags, const char *name)
{
    (void)flags;
    return alloc_native(out, size, name);
}

int buffer_malloc_withname_cached(struct fh_vmm_buffer *out, uint32_t size,
                                  uint32_t flags, const char *name)
{
    /*
     * FH8626 cache-maintenance equivalence is not yet recovered.  Majestic
     * staging uses the same mapped allocation and records this as an explicit
     * compatibility limitation rather than invoking the FH8852 cache ioctl.
     */
    return buffer_malloc_withname(out, size, flags, name);
}

int buffer_malloc(struct fh_vmm_buffer *out, uint32_t size)
{
    return alloc_native(out, size, "majestic-vmm");
}

static int unsupported(const char *name)
{
    if (trace_enabled())
        fprintf(stderr, "fh8626-vmm-compat: unsupported direct API %s\n", name);
    return -ENOSYS;
}

int FH_SYS_VmmAlloc(void *a, ...)
{
    (void)a;
    return unsupported("FH_SYS_VmmAlloc");
}

int FH_SYS_VmmAllocEx(void *a, ...)
{
    (void)a;
    return unsupported("FH_SYS_VmmAllocEx");
}

int FH_SYS_VmmAlloc_Cached(void *a, ...)
{
    (void)a;
    return unsupported("FH_SYS_VmmAlloc_Cached");
}

int FH_SYS_VmmAllocEx_Cached(void *a, ...)
{
    (void)a;
    return unsupported("FH_SYS_VmmAllocEx_Cached");
}

int FH_SYS_VmmFree(void *a, ...)
{
    (void)a;
    return unsupported("FH_SYS_VmmFree");
}

/* The FH8852 donor itself implements these three as zero-return stubs. */
void *FH_SYS_Mmap(uint32_t phys, uint32_t size)
{
    (void)phys; (void)size;
    return NULL;
}

int FH_SYS_Munmap(void *addr, uint32_t size)
{
    (void)addr; (void)size;
    return 0;
}

void *FH_SYS_GetVRegAddr(uint32_t base)
{
    (void)base;
    return NULL;
}

int FH_SYS_VmmGetPaddr_ByVaddr(void *virt, uint32_t *phys)
{
    (void)virt;
    if (phys)
        *phys = 0;
    return unsupported("FH_SYS_VmmGetPaddr_ByVaddr");
}

int FH_SYS_CloseFd(void)
{
    if (vmm_fd >= 0) {
        close(vmm_fd);
        vmm_fd = -1;
    }
    return 0;
}
