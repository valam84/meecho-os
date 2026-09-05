/*
 * Stopping: idling the core, and taking the machine down.
 */

#include "kernel/kernel.h"

#include <assert.h>
#include <sys/reboot.h>

#include <machine/vm.h>

#include "archconst.h"
#include "arch_proto.h"
#include "direct_utils.h"
#include "psci.h"

#include "bsp_reset.h"

/*===========================================================================*
 *				halt_cpu				     *
 *===========================================================================*/
void
halt_cpu(void)
{
	/*
	 * Wait for an interrupt with interrupts enabled, then mask them again
	 * before returning, because the caller was running with them masked.
	 *
	 * The order matters and is the same as ARM's. Interrupts have to be
	 * unmasked before the wfi and not after it, or the interrupt this is
	 * waiting for could arrive in between and be taken only after the
	 * core has already gone to sleep - which on a single-processor
	 * machine with one timer means it never wakes up. wfi itself does not
	 * need interrupts unmasked to return, but the handler does need to
	 * run.
	 *
	 * The dsb makes sure the writes that led to this point - marking the
	 * process idle, above all - are visible before the core stops.
	 */
	__asm__ volatile("dsb sy" ::: "memory");
	__asm__ volatile("msr daifclr, #2");	/* unmask IRQ */
	__asm__ volatile("wfi");
	__asm__ volatile("msr daifset, #2");	/* mask IRQ */
}

/*===========================================================================*
 *				reset					     *
 *===========================================================================*/
__dead void
reset(void)
{
	bsp_reset();		/* should not return */

	direct_print("Reset not supported on this machine.\n");
	for (;;)
		;
}

/*===========================================================================*
 *				poweroff				     *
 *===========================================================================*/
static __dead void
poweroff(void)
{
	bsp_poweroff();		/* should not return */

	direct_print("Unable to power this machine off.\n");
	for (;;)
		;
}

/*===========================================================================*
 *				arch_shutdown				     *
 *===========================================================================*/
__dead void
arch_shutdown(int how)
{
	/*
	 * On the virt machine all three of these actually work, because PSCI
	 * is there and QEMU implements it: a power-off exits the emulator
	 * rather than leaving it spinning. That is worth having beyond
	 * tidiness - it is what lets a test run end on its own instead of on
	 * a timeout.
	 */
	if ((how & RB_POWERDOWN) == RB_POWERDOWN)
		poweroff();

	if (how & RB_HALT) {
		for (;;)
			halt_cpu();
	}

	reset();
}
