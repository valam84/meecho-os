/* Стенд sunxi: вызовы ядра, которые нужны драйверу. MMU стенда отображает
 * память один к одному, поэтому физический адрес равен виртуальному. */
#ifndef _H_MINIX_SYSLIB_H
#define _H_MINIX_SYSLIB_H
#include <minix/type.h>
#define AC_ALIGN4K	0x01
void *vm_map_phys(endpoint_t who, void *phys, size_t len);
int vm_unmap_phys(endpoint_t who, void *vir, size_t len);
void *alloc_contig(size_t len, int flags, phys_bytes *phys);
int sys_cachectl(int op, void *addr, size_t len);
int sys_irqsetpolicy(int irq, int policy, int *hook);
int sys_irqrmpolicy(int *hook);
int sys_irqenable(int *hook);
int sys_irqdisable(int *hook);
int sys_setalarm(unsigned long ticks, int abs);
int sys_safecopyto(endpoint_t e, cp_grant_id_t g, vir_bytes off,
	vir_bytes addr, size_t bytes);
#endif
