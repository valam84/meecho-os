#ifndef _STUB_MINIX_SYSLIB_H
#define _STUB_MINIX_SYSLIB_H
#include <minix/type.h>
/* Настоящее значение MINIX, чтобы не совпало ни с одним чужим endpoint. */
#define SELF ((int)0x8ace)
void *vm_map_phys(int who, void *phys, size_t len);
int vm_unmap_phys(int who, void *vir, size_t len);
int sys_safecopyto(int dst, int grant, vir_bytes off, vir_bytes addr,
	size_t bytes);
int sys_safecopyfrom(int src, int grant, vir_bytes off, vir_bytes addr,
	size_t bytes);

/* Настоящее значение MINIX: выравнивание на страницу. */
#define AC_ALIGN4K	0x01
void *alloc_contig(size_t len, int flags, phys_bytes *phys);
void free_contig(void *addr, size_t len);
#endif
