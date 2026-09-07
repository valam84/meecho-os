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
/* What the caches are like: line size (CTR_EL0) and levels (CLIDR_EL1). */
int
sys_cacheinfo(unsigned long *ctr, unsigned long *clidr)
{
	message m;
	int r;

	memset(&m, 0, sizeof(m));
	m.SCACHE_OP = CACHE_INFO;

	if ((r = _kernel_call(SYS_CACHECTL, &m)) != OK)
		return r;

	*ctr = (unsigned long)m.SCACHE_ADDR;
	*clidr = (unsigned long)m.SCACHE_LEN;
	return OK;
}

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
