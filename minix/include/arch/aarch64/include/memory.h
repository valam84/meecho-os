/* Physical memory layout */

#ifndef _AARCH64_MEMORY_H
#define _AARCH64_MEMORY_H

/*
 * The range the kernel is prepared to treat as RAM.
 *
 * On earm the one supported board fixes these. AArch64 boards put their
 * memory in different places - QEMU's virt machine at 0x4000_0000, the
 * BCM2711 at 0 - and the real map arrives in the device tree at boot. These
 * are therefore only the outer bounds: everything a Cortex-A72 can address,
 * which is 44 bits.
 */
#define PHYS_MEM_BEGIN 0x0UL
#define PHYS_MEM_END   0xfffffffffffUL

/*
 * How much of that the kernel is willing to hand on.
 *
 * The limit is VM's, not the kernel's. phys_bytes is 64 bits here, the
 * memory map has always carried 64-bit values, and nothing in the kernel
 * would notice memory above 4 GiB. But VM hands out physical pages through
 * the two-level, 32-bit page tables of servers/vm/pt.c, written for i386 and
 * still unported; memory it cannot address would not be refused, it would be
 * folded back on itself.
 *
 * So add_memmap() truncates here, and this is the constant to raise - to
 * PHYS_MEM_END - once pt.c understands four levels of tables. A Compute
 * Module 4 with 8 GiB already loses half its memory to this, which is the
 * measure of how much that port is worth.
 */
#define VM_MAX_PHYS_MEM 0x100000000UL

#endif /* _AARCH64_MEMORY_H */
