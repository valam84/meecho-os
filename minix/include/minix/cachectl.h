#ifndef _MINIX_CACHECTL_H
#define _MINIX_CACHECTL_H

/*
 * The data cache maintenance operations of sys_cachectl(2), for drivers that
 * share a buffer with a device that does not look in the caches.
 *
 * On a machine whose DMA is coherent - a PC, say - none of this is needed and
 * the call answers OK having done nothing.  On one whose DMA is not, which is
 * every ARM SoC in sight, a buffer handed to a device and a buffer written by
 * a device are each wrong in their own way unless the driver says so here.
 *
 * Which operation goes where is not a matter of taste.  Named after what they
 * do to the cache rather than after a transfer direction, because a driver
 * that thinks in directions gets the partial-line case wrong:
 *
 *   CACHE_CLEAN		before the device READS the buffer.
 *				Dirty lines go to memory, the lines stay
 *				valid.  Always safe: nothing is discarded.
 *
 *   CACHE_INVALIDATE		before, and again after, the device WRITES the
 *				buffer.  Before, so that stale lines cannot be
 *				written back over what the device deposits;
 *				after, because a speculating core may have
 *				refilled a line while the transfer was in
 *				flight - and every ARMv8 core speculates.
 *
 *   CACHE_CLEAN_INVALIDATE	a buffer the device both reads and writes.
 *
 * A DMA buffer should be cache-line aligned and a whole number of lines long.
 * When it is not, the first and last lines of an invalidate also hold bytes
 * that are none of the device's business and may be dirty, so the kernel
 * cleans those two lines before discarding them - the rule NetBSD's
 * _bus_dmamap_sync_segment() spells out in sys/arch/arm/arm32/bus_dma.c, and
 * the one a hand-written driver forgets.  It cannot save a neighbour that is
 * written *during* the transfer; alignment is still the driver's job.
 *
 * The range is given in the caller's own address space, and the caller must
 * have it mapped - writably, for the two operations that discard.  That is
 * what says the memory is the caller's to act on: a physical range would let
 * any holder of this call throw away anyone's dirty data.
 */
#define CACHE_CLEAN		1
#define CACHE_INVALIDATE	2
#define CACHE_CLEAN_INVALIDATE	3

/*
 * Not an operation on memory: a question about the caches.  The reply
 * carries the two identification registers a driver cannot read for itself
 * - CTR_EL0, which says how long a cache line is, and CLIDR_EL1, which says
 * how many levels there are and where coherency starts - in the fields that
 * were the address and the length.  A driver that shares memory with a
 * device has to know the line size to lay its descriptors out; guessing it
 * is what this replaces.  On an architecture that has no such registers the
 * reply is zeroes.
 */
#define CACHE_INFO		4

#endif /* _MINIX_CACHECTL_H */
