/*
 * AArch64 address translation.
 *
 * Layout of the virtual address space, decided at stage 2.3 and recorded in
 * port/PORTING-LOG.md:
 *
 *	0x0000_0000_0000_0000 .. 0x0000_ffff_ffff_ffff	TTBR0_EL1, 256 TiB
 *		User space, once there is any. During boot it briefly holds
 *		an identity map of the kernel image, dropped as soon as the
 *		kernel is running from the upper half.
 *
 *	0xffff_0000_0000_0000 .. 0xffff_ffff_ffff_ffff	TTBR1_EL1, 256 TiB
 *		Kernel. Physical memory appears at a fixed offset, so
 *		translating between the two directions is an addition rather
 *		than a page table walk.
 *
 * The Cortex-A72 implements a 44-bit physical address space, so the linear
 * map can never need more than 0xffff_0000_0000_0000 .. 0xffff_0fff_ffff_ffff.
 * Everything above that is free for mappings that are not the linear map -
 * ioremap, vmalloc, fixmap - when they become necessary.
 */

#ifndef _AARCH64_MMU_H_
#define _AARCH64_MMU_H_

#include <stdint.h>

/*
 * The offset between physical and kernel-virtual addresses. Defined by the
 * build system rather than here, because the linker script needs the same
 * number and the two must not be able to drift apart: see Makefile.bringup.
 */
#ifndef KERNEL_VA_OFFSET
#error KERNEL_VA_OFFSET must be supplied by the build
#endif

#define PAGE_SHIFT	12
#define PAGE_SIZE	(1UL << PAGE_SHIFT)
#define PAGE_MASK	(PAGE_SIZE - 1)

static inline uint64_t
phys_to_virt(uint64_t pa)
{
	return pa + KERNEL_VA_OFFSET;
}

static inline uint64_t
virt_to_phys(uint64_t va)
{
	return va - KERNEL_VA_OFFSET;
}

/*
 * Build the translation tables and set SCTLR_EL1.M. Runs with the MMU off and
 * returns with it on, still executing from the identity map.
 */
void mmu_setup(void);

/*
 * Move to the upper half: switch the stack and branch to the kernel-virtual
 * alias of entry. Must be called from the identity map, exactly once, and
 * does not return.
 *
 * The argument is the address of the target as the caller sees it, which
 * before the switch means its physical address; the offset is added here.
 */
void mmu_switch_high(void (*entry)(void)) __attribute__((noreturn));

/* Take the identity map away, so that a stale physical pointer faults. */
void mmu_drop_identity(void);

/* Print the translation control registers. */
void mmu_report(void);

/*
 * Ask the hardware to translate va the way the given access would, and store
 * the physical address it produces. Returns 0 if the walk faults - which
 * covers both a missing mapping and one whose permissions forbid the access,
 * so the same call also reports whether the permissions took effect.
 *
 * This is the only honest way to read a mapping back. Walking the tables in
 * software would report what we believe we wrote, which is the thing in
 * doubt.
 */
#define MMU_ACCESS_EL1_READ	0
#define MMU_ACCESS_EL1_WRITE	1
#define MMU_ACCESS_EL0_READ	2

int mmu_probe(uint64_t va, unsigned access, uint64_t *pa);

#endif /* _AARCH64_MMU_H_ */
