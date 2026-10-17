/*
 * SYS_UMAP_REMOTE for a GPU service: the physical address of bytes at vir
 * in process ep - sys_umap_remote(ep, SELF, VM_D, ...), which libsys has
 * on i386 only.  The kernel answers only for a physically contiguous range
 * of pages the process has in its table.  A file of its own so that a host
 * stand (port/test/mali) can give its model's answer instead.
 *
 * Moved here from the mali service (mali_umap.c).
 */
#include <minix/drivers.h>
#include <minix/syslib.h>
#include <minix/drmdriver.h>

#include <string.h>

int
drmdrv_umap_remote(endpoint_t ep, vir_bytes vir, vir_bytes bytes,
	phys_bytes *phys)
{
	message m;
	int r;

	memset(&m, 0, sizeof(m));
	m.m_lsys_krn_sys_umap.src_endpt = ep;
	m.m_lsys_krn_sys_umap.dst_endpt = SELF;
	m.m_lsys_krn_sys_umap.segment = VM_D;
	m.m_lsys_krn_sys_umap.src_addr = vir;
	m.m_lsys_krn_sys_umap.nr_bytes = bytes;
	r = _kernel_call(SYS_UMAP_REMOTE, &m);
	*phys = m.m_krn_lsys_sys_umap.dst_addr;
	return r;
}
