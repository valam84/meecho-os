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

#endif /* _AARCH64_MEMORY_H */
