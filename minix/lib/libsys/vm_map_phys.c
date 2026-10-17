#include "syslib.h"

#include <sys/mman.h>
#include <minix/vm.h>
#include <stdarg.h>
#include <string.h>

static void *
map_phys(endpoint_t who, void *phaddr, size_t len, int flags)
{
	message m;
	int r;

	memset(&m, 0, sizeof(m));
	m.m_lsys_vm_map_phys.ep = who;
	m.m_lsys_vm_map_phys.phaddr = (phys_bytes)phaddr;
	m.m_lsys_vm_map_phys.len = len;
	m.m_lsys_vm_map_phys.flags = flags;

	r = _taskcall(VM_PROC_NR, VM_MAP_PHYS, &m);

	if (r != OK) return MAP_FAILED;

	r = sef_llvm_add_special_mem_region(m.m_lsys_vm_map_phys.reply,
	    len, NULL);
	if(r < 0) {
	    printf("vm_map_phys: add_special_mem_region failed: %d\n", r);
	}

	return m.m_lsys_vm_map_phys.reply;
}

void *
vm_map_phys(endpoint_t who, void *phaddr, size_t len)
{
	return map_phys(who, phaddr, len, 0);
}

/* The same, write-combining: memory only written, such as a frame being
 * scanned out (VM_MAP_PHYS_WC). */
void *
vm_map_phys_wc(endpoint_t who, void *phaddr, size_t len)
{
	return map_phys(who, phaddr, len, VM_MAP_PHYS_WC);
}

int
vm_unmap_phys(endpoint_t who, void *vaddr, size_t len)
{
	message m;
	int r;

	memset(&m, 0, sizeof(m));
	m.m_lsys_vm_unmap_phys.ep = who;
	m.m_lsys_vm_unmap_phys.vaddr = vaddr;

	r = _taskcall(VM_PROC_NR, VM_UNMAP_PHYS, &m);

	if(r != OK) return r;

	r = sef_llvm_del_special_mem_region_by_addr(vaddr);
	if(r < 0) {
	    printf("vm_map_phys: del_special_mem_region failed: %d\n", r);
	}

	return OK;
}
