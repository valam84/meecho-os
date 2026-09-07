/*
 * Data cache maintenance by address range, for sharing memory with a device
 * that does not look in the caches.
 *
 * This file is the arithmetic and the three instructions, and nothing else:
 * no address spaces, no processes, no message.  arch_cache_range() in
 * memory.c is what walks a caller's range and calls in here page by page;
 * port/test/cachectl runs the code below on a host, with the instructions
 * replaced by a recorder, because the one thing that cannot be checked on
 * the machine is which lines were touched.  QEMU does not model caches at
 * all, so nothing here is verified by running the system - only by the
 * reference it follows and by that stand.
 *
 * The reference is NetBSD's _bus_dmamap_sync_segment() in
 * sys/arch/arm/arm32/bus_dma.c, which the tree carries.  What it knows and a
 * derivation does not: on an invalidate, the first and last lines of a range
 * that does not begin and end on a line boundary must be cleaned as well as
 * invalidated, because they also hold bytes outside the range.  Getting that
 * wrong loses somebody else's dirty data, rarely, and looks like memory
 * corruption from a device driver.
 *
 * Everything here is to the Point of Coherency - the "AC" in cvac, ivac,
 * civac - which is the level a device sees.  Cleaning to the Point of
 * Unification, which is what sync_icache() in arch_system.c does, would only
 * reach the other cache on this core.
 */

#ifndef CACHE_TEST
#include "kernel/kernel.h"

#include <minix/cachectl.h>

#include "archconst.h"
#include "arch_proto.h"

/*
 * The three operations, as the architecture spells them.  The mnemonic is
 * part of the instruction, so this is three loops and not one with an
 * argument.
 */
#define DC_CLEAN(addr)		__asm__ volatile("dc cvac, %0"		\
				    :: "r"(addr) : "memory")
#define DC_INVALIDATE(addr)	__asm__ volatile("dc ivac, %0"		\
				    :: "r"(addr) : "memory")
#define DC_FLUSH(addr)		__asm__ volatile("dc civac, %0"		\
				    :: "r"(addr) : "memory")

/*
 * CTR_EL0.DminLine is the log2 of the smallest data cache line in the
 * system, counted in words.  The smallest is the one to use: a loop stepping
 * by it covers every line of every larger cache.
 */
static unsigned long
dcache_line_size(void)
{
	unsigned long ctr;

	__asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));

	return 4UL << ((ctr >> 16) & 0xf);
}

#define CACHE_BARRIER()		dsb()

#endif /* !CACHE_TEST */

/* Every line the half-open range [start, end) touches. */
#define FOR_EACH_LINE(a, start, end, line)				\
	for ((a) = (start) & ~((line) - 1); (a) < (end); (a) += (line))

static void
dcache_clean(unsigned long start, unsigned long end, unsigned long line)
{
	unsigned long a;

	FOR_EACH_LINE(a, start, end, line)
		DC_CLEAN(a);
}

static void
dcache_invalidate(unsigned long start, unsigned long end, unsigned long line)
{
	unsigned long a;

	FOR_EACH_LINE(a, start, end, line)
		DC_INVALIDATE(a);
}

static void
dcache_flush(unsigned long start, unsigned long end, unsigned long line)
{
	unsigned long a;

	FOR_EACH_LINE(a, start, end, line)
		DC_FLUSH(a);
}

/*===========================================================================*
 *				arch_cache_info				     *
 *===========================================================================*/
/*
 * The two registers that describe the caches, for a driver that asks.  Only
 * EL1 can read them here: SCTLR_EL1.UCT is not set, so an EL0 read of
 * CTR_EL0 traps.
 */
#ifndef CACHE_TEST
void
arch_cache_info(vir_bytes *ctr, vir_bytes *clidr)
{
	unsigned long v;

	__asm__ volatile("mrs %0, ctr_el0" : "=r"(v));
	*ctr = v;
	__asm__ volatile("mrs %0, clidr_el1" : "=r"(v));
	*clidr = v;
}
#endif /* !CACHE_TEST */

/*===========================================================================*
 *				dcache_range				     *
 *===========================================================================*/
/*
 * One operation over one contiguous range of kernel-addressable memory.
 *
 * The caller has already made sure the range is memory it may act on; this
 * only decides which instruction each line gets.
 */
void
dcache_range(int op, unsigned long addr, unsigned long len)
{
	unsigned long line = dcache_line_size();
	unsigned long mask = line - 1;
	unsigned long start = addr;
	unsigned long end = addr + len;

	if (len == 0)
		return;

	/*
	 * Ordering, both ways round.  Before: whatever the driver did to
	 * this memory, and whatever it read from the device to learn the
	 * transfer was over, has to be done before the lines are touched.
	 * After: the maintenance has to be finished before the driver rings
	 * the doorbell or reads a byte.  "sy" and not "ish" because the other
	 * party is a device, which is in no shareability domain of ours.
	 */
	CACHE_BARRIER();

	switch (op) {
	case CACHE_CLEAN:
		dcache_clean(start, end, line);
		break;

	case CACHE_CLEAN_INVALIDATE:
		dcache_flush(start, end, line);
		break;

	case CACHE_INVALIDATE: {
		/*
		 * A line at either end that the range only partly covers
		 * holds bytes belonging to somebody else, and those bytes
		 * may be dirty.  Discarding such a line would throw them
		 * away, so it is written back first.  Only the whole lines
		 * between the two ends are discarded outright.
		 *
		 * The three steps run in address order, lowest first.  That
		 * is not needed for correctness - they touch different lines
		 * - but a sweep is what a reader of a trace expects to see,
		 * and it is what the test stand checks against.
		 */
		unsigned long tail = end;

		if (start & mask) {
			unsigned long next = (start | mask) + 1;

			/* All of it in one line: write that line back. */
			if (next >= end) {
				dcache_flush(start, end, line);
				break;
			}

			dcache_flush(start, next, line);
			start = next;
		}

		/* start is line-aligned from here on. */
		if (end & mask) {
			unsigned long last = end & ~mask;

			/* What is left is inside one line. */
			if (last <= start) {
				dcache_flush(start, end, line);
				break;
			}

			end = last;
		}

		if (start < end)
			dcache_invalidate(start, end, line);

		if (end != tail)
			dcache_flush(end, tail, line);
		break;
	}
	}

	CACHE_BARRIER();
}
