#define _SYSTEM 1
#define _MINIX_SYSTEM 1
#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>
#include <minix/u64.h>
#include <minix/vm.h>

/* INCLUDES HERE */

#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/ioc_mmap.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>

#ifdef __weak_alias
__weak_alias(mmap, _mmap)
__weak_alias(munmap, _munmap)
#endif

void *minix_mmap_for(endpoint_t forwhom,
	void *addr, size_t len, int prot, int flags, int fd, off_t offset)
{
	message m;
	int r;

	memset(&m, 0, sizeof(m));
	m.m_mmap.addr = addr;
	m.m_mmap.len = len;
	m.m_mmap.prot = prot;
	m.m_mmap.flags = flags;
	m.m_mmap.fd = fd;
	m.m_mmap.offset = offset;
	m.m_mmap.forwhom = forwhom;

	if(forwhom != SELF) {
		m.m_mmap.flags |= MAP_THIRDPARTY;
	}

	r = _syscall(VM_PROC_NR, VM_MMAP, &m);

	if(r != OK) {
		return MAP_FAILED;
	}

	return m.m_mmap.retaddr;
}

int minix_vfs_mmap(endpoint_t who, off_t offset, size_t len,
	dev_t dev, ino_t ino, int fd, u32_t vaddr, u16_t clearend,
	u16_t flags)
{
	message m;

	memset(&m, 0, sizeof(message));

	m.m_vm_vfs_mmap.who = who;
	m.m_vm_vfs_mmap.offset = offset;
	m.m_vm_vfs_mmap.dev = dev;
	m.m_vm_vfs_mmap.ino = ino;
	m.m_vm_vfs_mmap.vaddr = vaddr;
	m.m_vm_vfs_mmap.len = len;
	m.m_vm_vfs_mmap.fd = fd;
	m.m_vm_vfs_mmap.clearend = clearend;
	m.m_vm_vfs_mmap.flags = flags;

	return _syscall(VM_PROC_NR, VM_VFS_MMAP, &m);
}

/*
 * A character device: VM refuses it with ENODEV (it has nothing to map;
 * VFS says so), and only the driver knows the device's memory.  So the
 * same question goes to the driver, on the same descriptor
 * (<sys/ioc_mmap.h>).  A driver that does not know the request says
 * ENOTTY, and mmap(2) fails with ENODEV, as before; anything else it says
 * is its answer about this mapping.  Not in libminc: a system service
 * maps no device of another, and libminc has no ioctl(2).
 */
#ifndef _LIBMINC
static void *mmap_cdev(void *addr, size_t len, int prot, int flags,
	int fd, off_t offset)
{
	struct cdev_mmap cm;

	memset(&cm, 0, sizeof(cm));
	cm.cm_addr = addr;
	cm.cm_len = len;
	cm.cm_prot = prot;
	cm.cm_flags = flags;
	cm.cm_offset = offset;

	if (ioctl(fd, MIOCMMAP, &cm) != 0) {
		if (errno == ENOTTY)
			errno = ENODEV;
		return MAP_FAILED;
	}
	return cm.cm_ret;
}
#endif /* !_LIBMINC */

void *mmap(void *addr, size_t len, int prot, int flags,
	int fd, off_t offset)
{
	void *p;

	p = minix_mmap_for(SELF, addr, len, prot, flags, fd, offset);
#ifndef _LIBMINC
	if (p == MAP_FAILED && errno == ENODEV && fd >= 0 &&
	    !(flags & MAP_ANON))
		p = mmap_cdev(addr, len, prot, flags, fd, offset);
#endif
	return p;
}

int munmap(void *addr, size_t len)
{
	message m;

	memset(&m, 0, sizeof(m));
	m.VMUM_ADDR = addr;
	m.VMUM_LEN = len;

	return _syscall(VM_PROC_NR, VM_MUNMAP, &m);
}


void *vm_remap(endpoint_t d,
			endpoint_t s,
			void *da,
			void *sa,
			size_t size)
{
	message m;
	int r;

	memset(&m, 0, sizeof(m));
	m.m_lsys_vm_vmremap.destination = d;
	m.m_lsys_vm_vmremap.source = s;
	m.m_lsys_vm_vmremap.dest_addr = da;
	m.m_lsys_vm_vmremap.src_addr = sa;
	m.m_lsys_vm_vmremap.size = size;

	r = _syscall(VM_PROC_NR, VM_REMAP, &m);
	if (r != OK)
		return MAP_FAILED;
	return m.m_lsys_vm_vmremap.ret_addr;
}

void *vm_remap_ro(endpoint_t d,
			endpoint_t s,
			void *da,
			void *sa,
			size_t size)
{
	message m;
	int r;

	memset(&m, 0, sizeof(m));
	m.m_lsys_vm_vmremap.destination = d;
	m.m_lsys_vm_vmremap.source = s;
	m.m_lsys_vm_vmremap.dest_addr = da;
	m.m_lsys_vm_vmremap.src_addr = sa;
	m.m_lsys_vm_vmremap.size = size;

	r = _syscall(VM_PROC_NR, VM_REMAP_RO, &m);
	if (r != OK)
		return MAP_FAILED;
	return m.m_lsys_vm_vmremap.ret_addr;
}

int vm_unmap(endpoint_t endpt, void *addr)
{
	message m;

	memset(&m, 0, sizeof(m));
	m.m_lc_vm_shm_unmap.forwhom = endpt;
	m.m_lc_vm_shm_unmap.addr = addr;

	return _syscall(VM_PROC_NR, VM_SHM_UNMAP, &m);
}

unsigned long vm_getphys(endpoint_t endpt, void *addr)
{
	message m;
	int r;

	memset(&m, 0, sizeof(m));
	m.m_lc_vm_getphys.endpt = endpt;
	m.m_lc_vm_getphys.addr = addr;

	r = _syscall(VM_PROC_NR, VM_GETPHYS, &m);
	if (r != OK)
		return 0;
	return (unsigned long) m.m_lc_vm_getphys.ret_addr;
}

u8_t vm_getrefcount(endpoint_t endpt, void *addr)
{
	message m;
	int r;

	memset(&m, 0, sizeof(m));
	m.m_lsys_vm_getref.endpt = endpt;
	m.m_lsys_vm_getref.addr  = addr;

	r = _syscall(VM_PROC_NR, VM_GETREF, &m);
	if (r != OK)
		return (u8_t) -1;
	return (u8_t) m.m_lsys_vm_getref.retc;
}

