/*
 * Interrupt dispatch. See intr.h for what this stands in for.
 */

#include <stdint.h>

#include "bsp_intr.h"
#include "intr.h"
#include "kprint.h"

/*
 * One handler per interrupt. The table is sized for the largest INTID a
 * GICv2 can report; at eight bytes each that is 2 KiB of BSS, which is
 * cheaper than the arithmetic needed to avoid it.
 */
#define NR_IRQS		256

static irq_handler_t irq_handler[NR_IRQS];
static unsigned irq_taken;
static unsigned irq_unclaimed;

void
irq_register(int irq, irq_handler_t handler)
{
	if (irq < 0 || irq >= NR_IRQS) {
		kputs("irq: refusing to register out of range\n");
		return;
	}

	irq_handler[irq] = handler;
}

void
irq_handle(int irq)
{
	irq_taken++;

	if (irq >= 0 && irq < NR_IRQS && irq_handler[irq] != 0) {
		irq_handler[irq](irq);
		return;
	}

	/*
	 * Nobody wants it. Mask the line before returning: a level-triggered
	 * interrupt with no handler re-asserts the moment we leave, and the
	 * kernel would spend the rest of its life in the vector.
	 *
	 * Report only the first one. If this is happening it is happening a
	 * lot, and a console that scrolls forever hides the reason.
	 */
	if (irq_unclaimed++ == 0) {
		kputs("irq: no handler for ");
		kput_dec((uint64_t)irq);
		kputs(", masking it\n");
	}

	bsp_irq_mask(irq);
}

unsigned
irq_count(void)
{
	return irq_taken;
}

unsigned
irq_spurious_count(void)
{
	return irq_unclaimed;
}
