#ifndef FH8626_LIBMIPI_H
#define FH8626_LIBMIPI_H

#include <stddef.h>
#include <stdint.h>

/*
 * Source-level reimplementation of the retained FH8626 libmipi.so.
 * Target ABI is ARM32 little-endian EABI. mipi_common is intentionally kept
 * as four 32-bit mapped virtual addresses to match the exported 16-byte object.
 */
extern int mipi_mm_fd;
extern uint32_t mipi_common[4];

const char *get_mipi_version(int print_flag);
void FH_MIPI_Version(void);

int mipi_mm_init(void);
int mipi_mm_close(void);
int mipi_mmap(uint32_t physical, uint32_t *slot, size_t length);
void mipi_init(int *words);

#endif
