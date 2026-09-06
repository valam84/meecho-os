#ifndef __HW_INTR_AARCH64_H__
#define __HW_INTR_AARCH64_H__

#include "kernel/kernel.h"

/*
 * The interrupt controller as the generic kernel sees it. Six operations,
 * the same set ARM already had, because the generic kernel's view of an
 * interrupt controller is the same everywhere: mask a line, unmask it,
 * acknowledge one that has fired, and say whether a line is in use at all.
 *
 * Behind them on this architecture is a GIC, of whichever version the device
 * tree reports; see arch/aarch64/gic.c. Which one it is is not the generic
 * kernel's business.
 */
void irq_handle(int irq);

void hw_intr_mask(int irq);
void hw_intr_unmask(int irq);
void hw_intr_ack(int irq);
void hw_intr_used(int irq);
void hw_intr_not_used(int irq);
void hw_intr_disable_all(void);

/*
 * Retiring an IPI. Nothing to do here: an IPI is a software generated
 * interrupt and arrives through bsp_irq_handle() like any other, which writes
 * the end-of-interrupt itself once the handler has returned. i386 needs the
 * call because its IPIs have interrupt vectors of their own and never pass
 * through the shared path.
 */
#define ipi_ack()	do { } while (0)

#endif /* __HW_INTR_AARCH64_H__ */
