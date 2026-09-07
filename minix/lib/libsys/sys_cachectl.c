#include "syslib.h"

#include <string.h>

#include <minix/cachectl.h>

/*
 * Data cache maintenance over a range of the caller's own memory, so that a
 * device which does not look in the caches sees what the driver wrote, and
 * the driver sees what the device wrote.
 *
 * Which operation to ask for is in <minix/cachectl.h>; the answer on a
 * machine whose DMA is coherent is OK with nothing done.
 */
int
sys_cachectl(int op, void *addr, size_t len)
{
	message m;

	memset(&m, 0, sizeof(m));
	m.SCACHE_OP = op;
	m.SCACHE_ADDR = (vir_bytes)addr;
	m.SCACHE_LEN = len;

	return _kernel_call(SYS_CACHECTL, &m);
}
