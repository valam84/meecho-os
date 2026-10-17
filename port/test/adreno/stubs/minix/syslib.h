/* Host stand: the kernel calls the service makes, all answered by the
 * model (model.c). */
#ifndef _STUB_MINIX_SYSLIB_H
#define _STUB_MINIX_SYSLIB_H
#include <minix/type.h>
#define SELF ((int)0x8ace)
void *vm_map_phys(int who, void *phys, size_t len);
struct qcomscm_answer;
int sys_qcomscm(int op, uint32_t svc, uint32_t cmd,
	struct qcomscm_answer *ans);
#endif
