#ifndef _BSP_INTR_H_
#define _BSP_INTR_H_

#ifndef __ASSEMBLER__

#include "intr.h"

/*
 * Same four functions as the earm contract, and for the same reasons. The
 * signature of intr_init() is kept as the generic kernel's - main.c calls
 * intr_init(0) - even though there is no generic kernel here yet and nothing
 * to do with auto_eoi.
 */
int intr_init(const int auto_eoi);

void bsp_irq_unmask(int irq);
void bsp_irq_mask(const int irq);

/* How many interrupt lines the controller turned out to implement. */
int bsp_irq_lines(void);

/*
 * Take one interrupt: ask the controller what happened, dispatch it, tell the
 * controller it is finished. Called from the IRQ vector.
 */
void bsp_irq_handle(void);

#endif /* __ASSEMBLER__ */

#endif /* _BSP_INTR_H_ */
