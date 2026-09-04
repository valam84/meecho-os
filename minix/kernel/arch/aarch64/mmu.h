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
 * Ask for a range of device registers to be mapped, and for *slot to be kept
 * pointing at it.
 *
 * This is the bring-up form of kern_phys_map_ptr(), which the BSPs on earm
 * call from their init routines: the driver says where its registers are and
 * hands over the variable holding its base, and the address in that variable
 * is corrected once paging exists. There the correction comes from VM; here
 * it comes from mmu_activate_device_maps(), and there is no VM to ask.
 *
 * Must be called before mmu_setup(), which is what actually builds the
 * mappings. Until then *slot holds the physical address, which is the right
 * answer while the MMU is off.
 */
void mmu_map_device(uint64_t base, uint64_t size, uint64_t *slot);

/*
 * Repoint every registered device base at its kernel mapping. Call after the
 * move to the upper half and before the identity map is dropped - between
 * those two points either address works, which is the only window where the
 * change can be made without a driver losing its registers mid-write.
 */
void mmu_activate_device_maps(void);

/*
 * Walk what the BSP registered. Lets the boot path report the device
 * mappings without knowing which board it is on, which is the whole point of
 * the BSP being a separate thing.
 */
unsigned mmu_device_map_count(void);
int mmu_device_map_get(unsigned i, uint64_t *base, uint64_t *size);

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
 * User address spaces.
 *
 * A process lives in TTBR0, which mmu_drop_identity() switched off at the end
 * of boot. mmu_user_enter() turns it back on for one address space; every
 * mapping made through these is non-global, so its TLB entries are tagged
 * with the ASID and do not have to be shot down when another process runs.
 */
uint64_t mmu_user_create(void);
void mmu_user_map_text(uint64_t root, uint64_t va, uint64_t pa, uint64_t size);
void mmu_user_map_data(uint64_t root, uint64_t va, uint64_t pa, uint64_t size);
void mmu_user_enter(uint64_t root, unsigned asid);

/* The physical address of something in the kernel image. */
uint64_t mmu_kern_phys(const void *p);

/*
 * Make writes to a range visible to instruction fetch. Needed after putting
 * code somewhere - the data and instruction caches are not coherent with each
 * other, and QEMU will not tell you so.
 */
void mmu_sync_icache(uint64_t va, uint64_t size);

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
