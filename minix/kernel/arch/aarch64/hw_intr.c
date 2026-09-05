/*
 * The hardware-dependent half of interrupt handling: six calls the generic
 * kernel makes, each of which this architecture answers by asking the board's
 * controller.
 *
 * Four of them are empty, and that is not laziness. On i386 they carry the
 * 8259's quirks - acknowledging at the right one of two chips, remembering
 * which lines are in use so a spurious one can be told apart. A GIC needs
 * none of it: acknowledgement happens in bsp_irq_handle(), which reads IAR
 * and writes EOIR around the dispatch because the GIC requires the pair to be
 * on the same interrupt, not somewhere later; and a line that nobody uses is
 * simply a line that was never unmasked.
 */

#include "kernel/kernel.h"

#include "arch_proto.h"
#include "hw_intr.h"

#include "bsp_intr.h"

void
hw_intr_mask(int irq)
{
	bsp_irq_mask(irq);
}

void
hw_intr_unmask(int irq)
{
	bsp_irq_unmask(irq);
}

void
hw_intr_ack(int irq)
{
	/*
	 * Nothing: the GIC's end-of-interrupt is written by bsp_irq_handle(),
	 * with the full IAR value it acknowledged with. Retiring here instead
	 * would mean handing the CPU interface an INTID rather than that
	 * value, which is wrong for an SGI, and would retire the interrupt
	 * before the hook chain had run.
	 */
}

void
hw_intr_used(int irq)
{
	/* Nothing to reserve: a GIC line is in use once it is unmasked. */
}

void
hw_intr_not_used(int irq)
{
	/* And out of use once it is masked, which the caller has done. */
}

void
hw_intr_disable_all(void)
{
	int i, lines = bsp_irq_lines();

	/*
	 * Called on the way down. Masking each line one at a time rather than
	 * taking the whole distributor down, because this runs while the
	 * kernel is still using its own timer, and a controller switched off
	 * underneath a running kernel is harder to reason about than a set of
	 * masked lines.
	 */
	for (i = 0; i < lines; i++)
		bsp_irq_mask(i);
}
