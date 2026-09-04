#ifndef __SYS_VM_AARCH64_H__
#define __SYS_VM_AARCH64_H__
/*
aarch64/vm.h
*/

/*
 * The VMSAv8-64 translation tables as the kernel and VM set them up: 4 KiB
 * granule, 48-bit virtual addresses, four levels of tables.
 *
 * Every ARMv8 implementation must offer the 4 KiB granule and the
 * Cortex-A72 offers no 16 KiB one, and 4 KiB is the page size the rest of
 * MINIX already assumes. The layout was settled at stage 2.3 of the port;
 * see port/PORTING-LOG.md.
 *
 * The bring-up kernel in minix/kernel/arch/aarch64/mmu.c still carries a
 * private copy of these values. The two get merged when the kernel joins
 * the tree build.
 */

#define AARCH64_PAGE_SHIFT	12
#define AARCH64_PAGE_SIZE	(1UL << AARCH64_PAGE_SHIFT)
#define AARCH64_PAGE_MASK	(AARCH64_PAGE_SIZE - 1)
#define AARCH64_VA_BITS		48

#define AARCH64_VM_PT_ENTRIES	512	/* entries per table, every level */
#define AARCH64_VM_PT_ENT_SIZE	8	/* size of a table entry */
#define AARCH64_VM_PT_SIZE	(AARCH64_VM_PT_ENTRIES * AARCH64_VM_PT_ENT_SIZE)
#define AARCH64_PAGETABLE_SIZE	AARCH64_VM_PT_SIZE

/* Address bits translated at each level, counted from the top. */
#define AARCH64_VM_L0_SHIFT	39	/* 512 GiB per entry: tables only */
#define AARCH64_VM_L1_SHIFT	30	/* 1 GiB: block or table */
#define AARCH64_VM_L2_SHIFT	21	/* 2 MiB: block or table */
#define AARCH64_VM_L3_SHIFT	12	/* 4 KiB: page */
#define AARCH64_VM_LEVEL_SHIFT(l)	(AARCH64_VM_L0_SHIFT - 9 * (l))
#define AARCH64_VM_INDEX(va, l)	\
	(((va) >> AARCH64_VM_LEVEL_SHIFT(l)) & (AARCH64_VM_PT_ENTRIES - 1))

#define AARCH64_VM_L1_BLOCK_SIZE	(1UL << AARCH64_VM_L1_SHIFT)
#define AARCH64_VM_L2_BLOCK_SIZE	(1UL << AARCH64_VM_L2_SHIFT)

/* Descriptor type, bits [1:0]. */
#define AARCH64_VM_VALID	(1UL << 0)
#define AARCH64_VM_TABLE	(1UL << 1)	/* levels 0..2: next-level table */
#define AARCH64_VM_BLOCK	(0UL << 1)	/* levels 1..2: block mapping */
#define AARCH64_VM_PAGE		(1UL << 1)	/* level 3: page mapping */

/* Output address of a block, a page or a next-level table. */
#define AARCH64_VM_ADDR_MASK	0x0000fffffffff000UL
#define AARCH64_VM_PFA(e)	((e) & AARCH64_VM_ADDR_MASK)

/* Lower attributes of block and page descriptors. */
#define AARCH64_VM_ATTRINDX(n)	((unsigned long)(n) << 2)	/* MAIR index */
#define AARCH64_VM_NS		(1UL << 5)
#define AARCH64_VM_AP_RW_EL1	(0UL << 6)	/* EL1 read/write, EL0 no access */
#define AARCH64_VM_AP_RW_ALL	(1UL << 6)	/* EL1 and EL0 read/write */
#define AARCH64_VM_AP_RO_EL1	(2UL << 6)	/* EL1 read-only, EL0 no access */
#define AARCH64_VM_AP_RO_ALL	(3UL << 6)	/* EL1 and EL0 read-only */
#define AARCH64_VM_AP_MASK	(3UL << 6)
#define AARCH64_VM_SH_NONE	(0UL << 8)
#define AARCH64_VM_SH_OUTER	(2UL << 8)
#define AARCH64_VM_SH_INNER	(3UL << 8)
#define AARCH64_VM_AF		(1UL << 10)	/* access flag */
#define AARCH64_VM_NG		(1UL << 11)	/* not global: TLB entry carries the ASID */

/* Upper attributes. */
#define AARCH64_VM_CONTIG	(1UL << 52)
#define AARCH64_VM_PXN		(1UL << 53)	/* no execute at EL1 */
#define AARCH64_VM_UXN		(1UL << 54)	/* no execute at EL0 */

/* Hierarchical controls carried by table descriptors. */
#define AARCH64_VM_TBL_PXN	(1UL << 59)
#define AARCH64_VM_TBL_UXN	(1UL << 60)
#define AARCH64_VM_TBL_AP_NO_EL0	(1UL << 61)
#define AARCH64_VM_TBL_AP_RO	(2UL << 61)
#define AARCH64_VM_TBL_NS	(1UL << 63)

/*
 * MAIR_EL1 indices as the kernel programs them. Index 0 is the strictest
 * type on purpose: a descriptor assembled from a zeroed attribute field then
 * names memory that can be neither speculated into nor reordered, which is
 * the safe way to be wrong.
 */
#define AARCH64_MAIR_DEVICE_nGnRnE	0
#define AARCH64_MAIR_DEVICE_nGnRE	1
#define AARCH64_MAIR_NORMAL		2	/* inner+outer write-back, RW-allocate */
#define AARCH64_MAIR_NORMAL_NC		3	/* inner+outer non-cacheable */

#define AARCH64_VM_PTE_CACHED	\
	(AARCH64_VM_ATTRINDX(AARCH64_MAIR_NORMAL) | AARCH64_VM_SH_INNER)
#define AARCH64_VM_PTE_UNCACHED	\
	(AARCH64_VM_ATTRINDX(AARCH64_MAIR_NORMAL_NC) | AARCH64_VM_SH_INNER)
#define AARCH64_VM_PTE_DEVICE	\
	(AARCH64_VM_ATTRINDX(AARCH64_MAIR_DEVICE_nGnRnE) | AARCH64_VM_SH_NONE)

/* TTBRn_EL1: the ASID sits above the table address. */
#define AARCH64_TTBR_ADDR_MASK	0x0000fffffffffffeUL
#define AARCH64_TTBR_ASID_SHIFT	48
#define AARCH64_TTBR_ASID(a)	((unsigned long)(a) << AARCH64_TTBR_ASID_SHIFT)

/*
 * Data abort fault status: ESR_EL1.ISS.DFSC. For translation, access flag
 * and permission faults the low two bits hold the level at which the lookup
 * failed.
 */
#define AARCH64_VM_FSC_MASK		0x3f
#define AARCH64_VM_FSC_TYPE(fsc)	((fsc) & 0x3c)
#define AARCH64_VM_FSC_LEVEL(fsc)	((fsc) & 0x3)
#define AARCH64_VM_FSC_ADDR_SIZE	0x00	/* address size fault */
#define AARCH64_VM_FSC_TRANS		0x04	/* translation fault */
#define AARCH64_VM_FSC_ACCESS		0x08	/* access flag fault */
#define AARCH64_VM_FSC_PERM		0x0c	/* permission fault */
#define AARCH64_VM_FSC_EXT_ABORT	0x10	/* synchronous external abort */
#define AARCH64_VM_FSC_ALIGN		0x21	/* alignment fault */
#define AARCH64_VM_FSC_TLB_CONFLICT	0x30

/* ESR_EL1.ISS.WnR: the faulting access was a write. */
#define AARCH64_VM_ISS_WNR		(1UL << 6)

#define AARCH64_VM_PFE_IS_TRANS(fsc)	\
	(AARCH64_VM_FSC_TYPE(fsc) == AARCH64_VM_FSC_TRANS)
#define AARCH64_VM_PFE_IS_ACCESS(fsc)	\
	(AARCH64_VM_FSC_TYPE(fsc) == AARCH64_VM_FSC_ACCESS)
#define AARCH64_VM_PFE_IS_PERM(fsc)	\
	(AARCH64_VM_FSC_TYPE(fsc) == AARCH64_VM_FSC_PERM)

#ifndef __ASSEMBLY__

#include <minix/type.h>

/* structure used by VM to pass data to the kernel while enabling paging */
struct vm_ep_data {
	struct mem_map	* mem_map;
	vir_bytes	data_seg_limit;
};
#endif

#endif /* __SYS_VM_AARCH64_H__ */
