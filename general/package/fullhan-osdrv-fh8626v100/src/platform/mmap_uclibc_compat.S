    .syntax unified
    .text
    .align 2
    .global mmap
    .type mmap, %function
/*
 * uClibc-0.9.33 32-bit mmap ABI compatibility shim for vendor Fullhan .so files
 * loaded into a musl ARM process.
 *
 * Vendor call ABI:
 *   r0=addr, r1=len, r2=prot, r3=flags, [sp]=fd, [sp+4]=byte_offset32
 *
 * musl uses 64-bit off_t on 32-bit ARM, so resolving the vendor PLT directly to
 * musl mmap misreads the sixth argument.  Convert the old byte offset to the
 * Linux ARM mmap2 page offset and call musl syscall(192, ...).
 */
mmap:
    push    {r4, r5, r6, lr}
    mov     r4, r0              @ addr
    mov     r5, r3              @ flags
    ldr     r6, [sp, #16]       @ fd from caller stack
    ldr     r12, [sp, #20]      @ old 32-bit byte offset
    mov     r0, r12, lsl #20  @ isolate low 12 bits without non-encodable #0xfff
    cmp     r0, #0
    bne     1f

    mov     r12, r12, lsr #12   @ mmap2 pgoffset
    sub     sp, sp, #12
    str     r5, [sp, #0]        @ syscall arg5: flags
    str     r6, [sp, #4]        @ syscall arg6: fd
    str     r12, [sp, #8]       @ syscall arg7: pgoffset
    mov     r3, r2              @ prot
    mov     r2, r1              @ len
    mov     r1, r4              @ addr
    mov     r0, #192            @ __NR_mmap2 on ARM EABI
    bl      syscall
    add     sp, sp, #12
    pop     {r4, r5, r6, pc}

1:
    bl      __errno_location
    mov     r1, #22             @ EINVAL
    str     r1, [r0]
    mvn     r0, #0              @ MAP_FAILED
    pop     {r4, r5, r6, pc}
    .size mmap, .-mmap

    .section .note.GNU-stack,"",%progbits
