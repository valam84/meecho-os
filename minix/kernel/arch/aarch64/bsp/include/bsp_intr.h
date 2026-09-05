#ifndef _BSP_INTR_H_
#define _BSP_INTR_H_

/*
 * The interrupt controller, as the board support package provides it. The
 * same three calls ARM has, so that the contract keeps its shape across the
 * two architectures.
 *
 * bsp_irq_handle() is called from the IRQ vector with nothing decided yet: it
 * is the controller that knows which line fired and how to acknowledge it,
 * and on GICv2 both of those are reads and writes of the CPU interface
 * registers. Everything above it - the hook table, the notification to the
 * driver - is generic.
 *
 * The implementations arrive with group 3; the exception path calls into them
 * from group 4, which is why the header is here first.
 */
void bsp_irq_handle(void);
void bsp_irq_unmask(int irq);
void bsp_irq_mask(int irq);

#endif /* _BSP_INTR_H_ */
