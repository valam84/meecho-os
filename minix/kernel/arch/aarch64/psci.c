/*
 * PSCI: power control through the firmware below the kernel.
 *
 * See include/psci.h for what this is and why the conduit comes out of the
 * device tree. The whole file is two operations: find the node once, and
 * issue the call.
 */

#include "kernel/kernel.h"

#include <string.h>

#include <machine/vm.h>

#include "arch_proto.h"
#include <minix/fdt.h>
#include "psci.h"

/* Which instruction reaches the implementation. */
#define CONDUIT_UNKNOWN	0
#define CONDUIT_NONE	1
#define CONDUIT_HVC	2
#define CONDUIT_SMC	3

static int conduit = CONDUIT_UNKNOWN;

/*===========================================================================*
 *				compatible_with				     *
 *===========================================================================*/
/*
 * Whether a node's compatible property names the given string.
 *
 * The property is a list of NUL-terminated strings in one blob, most specific
 * first, so this cannot be a single strcmp: the psci node on QEMU says
 * "arm,psci-1.0", "arm,psci-0.2", "arm,psci", and the kernel cares that the
 * middle one is in there.
 */
static int
compatible_with(const struct fdt_node *node, const char *want)
{
	const char *list;
	unsigned len, off;

	if ((list = fdt_getprop(node, "compatible", &len)) == NULL)
		return 0;

	for (off = 0; off < len; off += strlen(list + off) + 1) {
		if (strcmp(list + off, want) == 0)
			return 1;
	}

	return 0;
}

/*===========================================================================*
 *				find_psci				     *
 *===========================================================================*/
static int
find_psci(void *cookie, int depth, const char *name,
	const struct fdt_node *node)
{
	const char *method;
	unsigned len;

	/*
	 * Matched on compatible rather than on the node's name. The name is
	 * "psci" on every tree seen so far, but it is the compatible string
	 * that the binding actually specifies.
	 */
	if (depth != 1 || !compatible_with(node, "arm,psci-0.2"))
		return 0;

	if ((method = fdt_getprop(node, "method", &len)) == NULL ||
	    len == 0 || method[len - 1] != '\0')
		return 0;

	if (strcmp(method, "hvc") == 0)
		*(int *)cookie = CONDUIT_HVC;
	else if (strcmp(method, "smc") == 0)
		*(int *)cookie = CONDUIT_SMC;

	return 1;	/* stop the walk either way: this was the node */
}

/*===========================================================================*
 *				psci_available				     *
 *===========================================================================*/
int
psci_available(void)
{
	const void *dtb;
	int found = CONDUIT_NONE;

	if (conduit != CONDUIT_UNKNOWN)
		return conduit != CONDUIT_NONE;

	/*
	 * Probed on demand rather than from arch_init(), because the first
	 * caller may well be a panic on the way out of pre_init(), before
	 * anything has been initialised. Answering "no PSCI" there would turn
	 * a clean power-off into a hang.
	 */
	conduit = CONDUIT_NONE;

	if (boot_dtb == 0)
		return 0;

	dtb = (const void *)phys2vir(boot_dtb);
	if (!fdt_valid(dtb))
		return 0;

	(void)fdt_walk(dtb, find_psci, &found);
	conduit = found;

	return conduit != CONDUIT_NONE;
}

/*===========================================================================*
 *				psci_call				     *
 *===========================================================================*/
long
psci_call(unsigned long fn, unsigned long a1, unsigned long a2,
	unsigned long a3)
{
	/*
	 * Asked before the argument registers are set up, and that ordering is
	 * load-bearing.
	 *
	 * The four variables below are bound to particular registers, and the
	 * compiler only promises to have the value there at the asm - not to
	 * keep it there across a call, which is free to clobber every one of
	 * them. With the test after the assignments, the first call was made
	 * with three of its four arguments holding whatever psci_available()
	 * had left behind; every call after it was fine, because by then the
	 * answer was cached and the function returned without doing anything.
	 *
	 * Which is why this was invisible until now: system_off() and
	 * system_reset() take no arguments and never return to check, so the
	 * first user of this that could notice was CPU_ON - and it noticed by
	 * failing to start exactly one core, the first.
	 */
	if (!psci_available())
		return PSCI_NOT_SUPPORTED;

	{
	register unsigned long x0 __asm__("x0") = fn;
	register unsigned long x1 __asm__("x1") = a1;
	register unsigned long x2 __asm__("x2") = a2;
	register unsigned long x3 __asm__("x3") = a3;

	/*
	 * The SMC calling convention returns in x0..x3 and may clobber the
	 * other caller-saved registers, which is what the compiler already
	 * assumes across an asm with no output constraints on them. The
	 * memory clobber is not optional: a system reset must not be
	 * reordered ahead of the writes that were meant to happen first.
	 */
	if (conduit == CONDUIT_HVC) {
		__asm__ volatile("hvc #0"
		    : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3)
		    :: "memory");
	} else {
		__asm__ volatile("smc #0"
		    : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3)
		    :: "memory");
	}

	return (long)x0;
	}
}

/*===========================================================================*
 *				psci_system_off				     *
 *===========================================================================*/
void
psci_system_off(void)
{
	(void)psci_call(PSCI_FN_SYSTEM_OFF, 0, 0, 0);
}

/*===========================================================================*
 *				psci_system_reset			     *
 *===========================================================================*/
void
psci_system_reset(void)
{
	(void)psci_call(PSCI_FN_SYSTEM_RESET, 0, 0, 0);
}

/*===========================================================================*
 *				psci_cpu_on				     *
 *===========================================================================*/
long
psci_cpu_on(u64_t mpidr, phys_bytes entry, unsigned long context)
{
	/*
	 * The core wakes at "entry" with its MMU off and "context" in x0.
	 * Which exception level it lands at is the firmware's business, so
	 * head.S normalises it there the same way it does for the boot core.
	 *
	 * Only the affinity fields of MPIDR name a core; the flag bits among
	 * them (U, MT, and the bit that reads as one) name nobody, and passing
	 * them through would ask the firmware to start a core that does not
	 * exist.
	 */
	return psci_call(PSCI_FN_CPU_ON,
	    (unsigned long)(mpidr & MPIDR_AFF_MASK),
	    (unsigned long)entry, context);
}
