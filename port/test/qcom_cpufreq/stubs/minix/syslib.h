/* Host stand: the one kernel call the service makes - mapping a block -
 * answered by the model (model.c), from its own memory. */
#ifndef _STUB_MINIX_SYSLIB_H
#define _STUB_MINIX_SYSLIB_H
#include <minix/type.h>
#define SELF ((int)0x8ace)
void *vm_map_phys(int who, void *phys, size_t len);
int vm_unmap_phys(int who, void *vaddr, size_t len);
#endif
