#ifndef __HW_INTR_AARCH64_H__
#define __HW_INTR_AARCH64_H__

#include "kernel/kernel.h"

/*
 * The interrupt controller as the generic kernel sees it. Six operations,
 * the same set ARM already had, because the generic kernel's view of an
 * interrupt controller is the same everywhere: mask a line, unmask it,
 * acknowledge one that has fired, and say whether a line is in use at all.
 *
 * Behind them on this architecture is a GIC. The bring-up kernel drives a
 * GICv2 in bsp/qemu-virt; a CM4 has a GIC-400, which is also GICv2. Which
 * one is behind these calls is the board support package's business, not
 * the generic kernel's.
 */
void irq_handle(int irq);

void hw_intr_mask(int irq);
void hw_intr_unmask(int irq);
void hw_intr_ack(int irq);
void hw_intr_used(int irq);
void hw_intr_not_used(int irq);
void hw_intr_disable_all(void);

#endif /* __HW_INTR_AARCH64_H__ */
