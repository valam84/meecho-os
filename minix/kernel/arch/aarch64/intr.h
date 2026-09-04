/*
 * Interrupt dispatch.
 *
 * The generic kernel does this in kernel/interrupt.c, where a handler is
 * int (*)(struct irq_hook *) and hooks chain so that several devices can
 * share a line. None of that exists yet: there is no struct proc to notify
 * and nothing shares a line. This is the same idea with the parts that need a
 * kernel removed, and it goes away when the generic kernel is linked in.
 *
 * irq_handle() keeps the name it has in the generic kernel, because that is
 * what the BSPs call and what they will keep calling.
 */

#ifndef _AARCH64_INTR_H_
#define _AARCH64_INTR_H_

/*
 * Bring-up stand-in for the generic kernel's irq_handler_t. That one takes
 * the hook it was registered with and returns whether the line should be
 * re-enabled; here the interrupt number is the only thing a handler could
 * want and there is nothing to decide.
 */
typedef void (*irq_handler_t)(int irq);

/* Attach a handler to an interrupt. Does not unmask it; the BSP does that. */
void irq_register(int irq, irq_handler_t handler);

/* Dispatch one interrupt. Called by the BSP once it knows which one it is. */
void irq_handle(int irq);

/* How many interrupts have been dispatched, and how many had no handler. */
unsigned irq_count(void);
unsigned irq_spurious_count(void);

#endif /* _AARCH64_INTR_H_ */
