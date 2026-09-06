#ifndef _AARCH64_CPUFUNC_H
#define _AARCH64_CPUFUNC_H

#include <machine/vm.h>

/*
 * The instructions the kernel needs that C has no spelling for. Kept small
 * on purpose: ARM's version of this file grew to four hundred lines of CP15
 * accessors because on ARMv7 every cache and TLB operation is a coprocessor
 * write with its own encoding. On AArch64 they are ordinary system
 * instructions, and the ones the kernel actually issues fit here.
 */

/*
 * Barriers.
 *
 * dsb waits for memory accesses to complete, isb makes the pipeline refetch.
 * A change to a system register that governs translation or exception entry
 * is not in force until both have run, which is why they come in that order
 * and why they carry a memory clobber: the compiler must not move accesses
 * across them either.
 *
 * The "sy" domain is the whole system. Narrower domains exist and are worth
 * having once SMP is real, but a wrong narrowing is a bug that appears once
 * a month on one machine, so the wide form stays until there is a measured
 * reason to narrow it.
 */
static inline void dsb(void)
{
	__asm__ volatile("dsb sy" ::: "memory");
}

static inline void dmb(void)
{
	__asm__ volatile("dmb sy" ::: "memory");
}

static inline void isb(void)
{
	__asm__ volatile("isb" ::: "memory");
}

/* A full barrier, for the generic code that just wants ordering. */
#define barrier()	do { dsb(); isb(); } while (0)

/*
 * Invalidate the whole TLB for EL1, then wait for it and resynchronise.
 *
 * vmalle1 is "all entries for this VMID at EL1", the AArch64 spelling of
 * what ARMv7 wrote as TLBIALL. The dsb before it orders the page table
 * writes ahead of the invalidation - the table walker reads memory, so an
 * invalidation that overtakes the store it was meant to publish leaves a
 * stale entry behind.
 */
static inline void refresh_tlb(void)
{
	dsb();
	__asm__ volatile("tlbi vmalle1" ::: "memory");
	dsb();
	isb();
}

/*
 * Every entry, on every core of the inner-shareable domain.
 *
 * refresh_tlb() above retires this core's entries only, which is all a core
 * needs for its own housekeeping. This one is for a mapping that changed
 * under a process which may have run anywhere - VMCTL_FLUSHTLB, where the
 * kernel is not even told whose mapping it was, so neither the core nor the
 * ASID can be narrowed down.
 */
static inline void refresh_tlb_all_is(void)
{
	dsb();
	__asm__ volatile("tlbi vmalle1is" ::: "memory");
	dsb();
	isb();
}

/*
 * Invalidate every entry belonging to one address space, on every core.
 *
 * aside1is is "all entries for this ASID, inner shareable", and the shareable
 * form is the point: an ASID names the same address space on every core, so
 * the core that decides those entries are stale is not necessarily the core
 * holding them. The local form would leave the others translating through a
 * table that no longer means what it did.
 *
 * The ASID goes in the top sixteen bits of the operand, the same place TTBR0
 * carries it.
 */
static inline void refresh_tlb_asid(unsigned asid)
{
	unsigned long arg = (unsigned long)asid << AARCH64_TTBR_ASID_SHIFT;

	dsb();
	__asm__ volatile("tlbi aside1is, %0" :: "r"(arg) : "memory");
	dsb();
	isb();
}

/* Invalidate one page, by virtual address, for the current translation. */
static inline void refresh_tlb_page(vir_bytes va)
{
	dsb();
	__asm__ volatile("tlbi vaae1, %0"
	    :: "r"(va >> AARCH64_PAGE_SHIFT) : "memory");
	dsb();
	isb();
}

/*
 * Read and write the translation table base registers. TTBR0 holds the user
 * half, TTBR1 the kernel half; the split is fixed by TCR_EL1 and does not
 * change after boot, so switching an address space is a write to TTBR0 alone.
 */
static inline unsigned long read_ttbr0(void)
{
	unsigned long v;

	__asm__ volatile("mrs %0, ttbr0_el1" : "=r"(v));
	return v;
}

static inline void write_ttbr0(unsigned long v)
{
	__asm__ volatile("msr ttbr0_el1, %0" :: "r"(v));
	isb();
}

static inline unsigned long read_ttbr1(void)
{
	unsigned long v;

	__asm__ volatile("mrs %0, ttbr1_el1" : "=r"(v));
	return v;
}

/*
 * Cache identification, for the code that has to keep the two caches in step
 * by hand. CTR_EL0.IDC says a data cache clean is not needed to make stores
 * visible to instruction fetch; CLIDR_EL1.LoUIS names the last cache level
 * that has to be cleaned when it is. See sync_icache() in arch_system.c.
 */
#define CTR_EL0_IDC		(1UL << 28)
#define CLIDR_LOUIS_SHIFT	21

/* The fault address and syndrome of the exception being handled. */
static inline unsigned long read_far(void)
{
	unsigned long v;

	__asm__ volatile("mrs %0, far_el1" : "=r"(v));
	return v;
}

static inline unsigned long read_esr(void)
{
	unsigned long v;

	__asm__ volatile("mrs %0, esr_el1" : "=r"(v));
	return v;
}

/* Which core this is, as the interrupt controller and per-CPU data see it. */
static inline unsigned long read_mpidr(void)
{
	unsigned long v;

	__asm__ volatile("mrs %0, mpidr_el1" : "=r"(v));
	return v;
}

#endif /* _AARCH64_CPUFUNC_H */
