/* The kernel call implemented in this file:
 *   m_type:	SYS_CACHECTL
 *
 * The parameters for this kernel call are:
 *	SCACHE_OP	which operation (CACHE_* in <minix/cachectl.h>)
 *	SCACHE_ADDR	start of the range, in the caller's address space
 *	SCACHE_LEN	its length in bytes
 *
 * Data cache maintenance, for a driver that shares a buffer with a device
 * which does not look in the caches.  On a machine whose DMA is coherent
 * this does nothing and says so; on one whose DMA is not, it is the only
 * thing that makes such a buffer mean the same to both sides.
 *
 * The range is the caller's own, and that is the whole of the permission
 * check: the address is translated through the caller's page table, so the
 * call can only reach memory the caller already has, and the two operations
 * that discard cache lines are refused on memory the caller may not write.
 * A physical range would have been simpler and would have let any holder of
 * this call throw away any process's dirty data.
 *
 * The call is separate from SYS_VMCTL for the same reason.  Cache
 * maintenance is wanted by device drivers, and VMCTL is the call that can
 * point any process's page table root at any physical address; granting a
 * network driver the second in order to give it the first would be a poor
 * trade.
 */

#include "kernel/system.h"

#include <minix/cachectl.h>

#if USE_CACHECTL

/*===========================================================================*
 *				do_cachectl				     *
 *===========================================================================*/
int
do_cachectl(struct proc * caller, message * m_ptr)
{
	vir_bytes addr = m_ptr->SCACHE_ADDR;
	vir_bytes len = m_ptr->SCACHE_LEN;
	int op = m_ptr->SCACHE_OP;

	switch (op) {
	case CACHE_CLEAN:
	case CACHE_INVALIDATE:
	case CACHE_CLEAN_INVALIDATE:
		break;
	case CACHE_INFO:
		arch_cache_info(&m_ptr->SCACHE_ADDR, &m_ptr->SCACHE_LEN);
		return OK;
	default:
		return EINVAL;
	}

	/* Nothing to do, and not an error: a zero-length transfer is legal. */
	if (len == 0)
		return OK;

	/* A range that wraps is not a range. */
	if (addr + len < addr)
		return EINVAL;

	return arch_cache_range(caller, addr, len, op);
}

#endif /* USE_CACHECTL */
