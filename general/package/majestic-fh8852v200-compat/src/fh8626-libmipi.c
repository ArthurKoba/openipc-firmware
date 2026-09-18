#define _GNU_SOURCE
#include "fh8626-libmipi.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

/* Exact initialized .data state. */
int mipi_mm_fd = -1;

/*
 * Exact exported object layout/order from stock:
 *   [0] -> mapping of 0xf0000000 / 0x38
 *   [1] -> mapping of 0xf1000000 / 0x0c
 *   [2] -> mapping of 0xf1100000 / 0x14c
 *   [3] -> mapping of 0xf0002000 / 0x44
 */
uint32_t mipi_common[4];

static int mipi_initialized;
static char mipi_version_buffer[72];

static volatile uint32_t *mapped(uint32_t value)
{
    return (volatile uint32_t *)(uintptr_t)value;
}

static uint32_t rd(volatile uint32_t *base, unsigned offset)
{
    return base[offset >> 2];
}

static void wr(volatile uint32_t *base, unsigned offset, uint32_t value)
{
    base[offset >> 2] = value;
}

const char *get_mipi_version(int print_flag)
{
    sprintf(mipi_version_buffer,
        "[mipi] version:\t%s(%s),build: %s",
        "V2.1.0", "gef34265", "2021-05-10");
    if (print_flag)
        puts(mipi_version_buffer);
    return mipi_version_buffer;
}

/*
 * Stock FH_MIPI_Version does not initialize r0 before calling
 * get_mipi_version, so its print flag is literally the caller's incoming r0.
 * Preserve that ARM ABI quirk with a tail branch. Non-ARM builds use a
 * deterministic no-print fallback for static/source analysis.
 */
#if defined(__arm__)
__attribute__((naked)) void FH_MIPI_Version(void)
{
    __asm__ volatile("b get_mipi_version");
}
#else
void FH_MIPI_Version(void)
{
    (void)get_mipi_version(0);
}
#endif

int mipi_mm_init(void)
{
    if (mipi_mm_fd < 0) {
        mipi_mm_fd = open("/dev/mem", 0x1002);
        if (mipi_mm_fd < 0) {
            perror("mipi mem open error");
            return -1;
        }
    }
    return 0;
}

int mipi_mm_close(void)
{
    if (mipi_mm_fd >= 0) {
        close(mipi_mm_fd);
        mipi_mm_fd = -1;
    }
    return 0;
}

int mipi_mmap(uint32_t physical, uint32_t *slot, size_t length)
{
    void *p;

    /* Stock ignores the return value and lets mmap report the failure. */
    (void)mipi_mm_init();

    if (*slot != 0u) {
        if (munmap((void *)(uintptr_t)*slot, length) == -1)
            perror("mipi mem unmap error\n");
    }

    p = mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED,
        mipi_mm_fd, (off_t)physical);
    *slot = (uint32_t)(uintptr_t)p;
    if (p == MAP_FAILED) {
        perror("mem mmap error\n");
        (void)mipi_mm_close();
        return -1;
    }

    (void)mipi_mm_close();
    return 0;
}

void mipi_init(int *words)
{
    volatile uint32_t *sys;
    volatile uint32_t *mode;
    volatile uint32_t *phy;
    volatile uint32_t *misc;
    uint32_t v;
    int lane_word;

    if (!mipi_initialized) {
        (void)get_mipi_version(1);
        mipi_common[0] = 0u;
        mipi_common[1] = 0u;
        mipi_common[2] = 0u;
        mipi_common[3] = 0u;

        /* Stock ignores mapping failures and marks the block initialized. */
        (void)mipi_mmap(0xf0000000u, &mipi_common[0], 0x38u);
        (void)mipi_mmap(0xf0002000u, &mipi_common[3], 0x44u);
        (void)mipi_mmap(0xf1000000u, &mipi_common[1], 0x0cu);
        (void)mipi_mmap(0xf1100000u, &mipi_common[2], 0x14cu);
        mipi_initialized = 1;
    }

    sys  = mapped(mipi_common[0]);
    mode = mapped(mipi_common[1]);
    phy  = mapped(mipi_common[2]);
    misc = mapped(mipi_common[3]);

    v = rd(sys, 0x0cu);
    wr(sys, 0x0cu, v | 0x00010000u);
    v = rd(sys, 0x24u);
    wr(sys, 0x24u, v & 0xffffe3ffu);
    v = rd(sys, 0x28u);
    wr(sys, 0x28u, (v & 0x003fff9fu) | 0x60u);

    wr(misc, 0x40u, 1u);

    wr(phy, 0x54u, 0u);
    wr(phy, 0x50u, 1u);
    wr(phy, 0x40u, 1u);
    wr(phy, 0x44u, 1u);
    wr(phy, 0x08u, 0xffffffffu);
    wr(phy, 0x50u, 0u);
    wr(phy, 0x50u, 0u);

    wr(phy, 0x54u, 0x00010034u);
    wr(phy, 0x50u, 2u);
    wr(phy, 0x50u, 0u);

    wr(phy, 0x54u, 0x14u);
    wr(phy, 0x50u, 2u);
    wr(phy, 0x50u, 0u);

    wr(phy, 0x54u, 0x00010044u);
    wr(phy, 0x50u, 2u);
    wr(phy, 0x50u, 0u);

    wr(phy, 0x54u, (uint32_t)words[0] << 1);
    wr(phy, 0x50u, 2u);
    wr(phy, 0x50u, 0u);

    usleep(100u);

    lane_word = words[5];
    wr(phy, 0x04u, 1u);
    if (lane_word == 1)
        wr(phy, 0x04u, 0u);
    else if (lane_word == 2)
        wr(phy, 0x04u, 1u);
    else if (lane_word == 4)
        wr(phy, 0x04u, 3u);

    wr(phy, 0x0e4u, 0xffffffffu);
    wr(phy, 0x0f4u, 0xffffffffu);
    wr(phy, 0x104u, 0xffffffffu);
    wr(phy, 0x114u, 0xffffffffu);
    wr(phy, 0x124u, 0xffffffffu);
    wr(phy, 0x134u, 0xffffffffu);

    v = rd(mode, 0x00u);
    if (words[3] != 0xff) {
        uint32_t word16 = words[3] == 0 ? 0u : 0x00010000u;
        v = ((uint32_t)words[4] & 3u) << 12 |
            ((uint32_t)words[1] & 3u) |
            (v & 0x000003f8u) |
            (((uint32_t)words[2] & 1u) << 2) |
            word16;
    } else {
        v = (v & 0xfffffff8u) |
            (uint32_t)words[1] |
            ((uint32_t)words[2] << 2);
    }
    wr(mode, 0x00u, v);
    wr(mode, 0x04u, 1u);
}
