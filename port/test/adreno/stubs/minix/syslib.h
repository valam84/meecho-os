/* Host stand: the kernel calls the service makes, all answered by the
 * model (model.c). */
#ifndef _STUB_MINIX_SYSLIB_H
#define _STUB_MINIX_SYSLIB_H
#include <minix/type.h>
#define SELF ((int)0x8ace)
void *vm_map_phys(int who, void *phys, size_t len);
int vm_unmap_phys(int who, void *vaddr, size_t len);
struct qcomscm_answer;
int sys_qcomscm(int op, uint32_t svc, uint32_t cmd,
	struct qcomscm_answer *ans);
int sys_qcomscm_mem(int op, uint32_t id, void *addr, size_t len,
	struct qcomscm_answer *ans);	/* tzmodel.c */

/* Memory the SMMU walks, and the processor's caches over it
 * (smmumodel.c). */
void *alloc_contig(size_t len, int flags, phys_bytes *phys);
int free_contig(void *addr, size_t len);
#define AC_ALIGN4K	0x01
int sys_cachectl(int op, void *addr, size_t len);

/* Interrupts and alarms (smmumodel.c). */
#define IRQ_SETPOLICY	1
#define IRQ_RMPOLICY	2
#define IRQ_ENABLE	3
#define IRQ_DISABLE	4
int sys_irqctl(int request, int irq_vec, int policy, int *irq_hook_id);
#define sys_irqenable(h)		sys_irqctl(IRQ_ENABLE, 0, 0, (h))
#define sys_irqdisable(h)		sys_irqctl(IRQ_DISABLE, 0, 0, (h))
#define sys_irqsetpolicy(v, p, h)	sys_irqctl(IRQ_SETPOLICY, (v), (p), (h))
#define sys_irqrmpolicy(h)		sys_irqctl(IRQ_RMPOLICY, 0, 0, (h))
int sys_setalarm2(clock_t exp_time, int abs_time, clock_t *time_left,
	clock_t *uptime);
#define sys_setalarm(e, a)		sys_setalarm2((e), (a), NULL, NULL)
#endif
