/*
 * What the kernel does with an exception, and what it can say about one.
 *
 * So far only the reporting half: the entry path, the vector table and the
 * fault handlers come over from arch/aarch64/bringup/trap.c and
 * exception.S with the rest of group 4.
 */

#include "kernel/kernel.h"
#include "kernel/proc.h"
#include "kernel/proto.h"

#include <assert.h>
#include <stdlib.h>
#include <machine/vm.h>

#include "archconst.h"
#include "arch_proto.h"

#if USE_SYSDEBUG

/* How far back to walk before giving up on a damaged or hostile chain. */
#define STACKTRACE_MAX_FRAMES	16

/*===========================================================================*
 *				read_frame				     *
 *===========================================================================*/
/*
 * One stack frame of a process: the caller's frame pointer and the return
 * address, which AAPCS64 puts at [x29] and [x29 + 8].
 *
 * Read through data_copy() rather than dereferenced. The addresses come out
 * of a stack that may well be the reason the kernel is printing a trace at
 * all, so every one of them is a pointer that has to be allowed to be wrong -
 * and data_copy() walks the process's own page tables to check, which is
 * exactly the question being asked. A trace that panicked would be worse than
 * no trace.
 */
static int
read_frame(struct proc *pr, reg_t fp, reg_t *next_fp, reg_t *ret_addr)
{
	reg_t frame[2];

	if (fp == 0 || (fp % sizeof(reg_t)) != 0)
		return 0;

	if (data_copy(pr->p_endpoint, fp, KERNEL, (vir_bytes)frame,
	    sizeof(frame)) != OK)
		return 0;

	*next_fp = frame[0];
	*ret_addr = frame[1];

	return 1;
}

/*===========================================================================*
 *				proc_stacktrace				     *
 *===========================================================================*/
void
proc_stacktrace(struct proc *whichproc)
{
	reg_t fp = whichproc->p_reg.fp;
	reg_t next_fp, ret_addr;
	int i;

	/*
	 * The link register alongside the program counter, which the 32-bit
	 * ports have no equivalent of and this architecture cannot do
	 * without. A leaf function leaves no frame, and code built with
	 * -fomit-frame-pointer - which every server in this system is -
	 * leaves no chain to walk at all, so the caller's return address is
	 * in x30 and nowhere else. It is the difference between "the process
	 * jumped to zero" and knowing what jumped it there.
	 */
	printf("%-8.8s %6d pc 0x%lx lr 0x%lx ", whichproc->p_name,
	    whichproc->p_endpoint, (unsigned long)whichproc->p_reg.pc,
	    (unsigned long)whichproc->p_reg.lr);

	/*
	 * Walk the frame pointer chain. ARM prints the program counter and
	 * stops, which is all a trace is there; this walks, because the
	 * chain is cheap to follow on AArch64 - x29 and x30 are saved as a
	 * pair by every non-leaf function - and because for the next stretch
	 * of this port a stack trace is most of the debugger there is.
	 *
	 * A leaf function has no frame, so the innermost entry printed may be
	 * its caller. That is a property of the chain, not a bug to work
	 * around by guessing.
	 */
	for (i = 0; i < STACKTRACE_MAX_FRAMES; i++) {
		if (!read_frame(whichproc, fp, &next_fp, &ret_addr))
			break;

		printf("0x%lx ", (unsigned long)ret_addr);

		/*
		 * The chain must go towards higher addresses, or it is not a
		 * chain: a frame pointing at itself or backwards is a corrupt
		 * stack, and following it is how a trace becomes a hang.
		 */
		if (next_fp <= fp)
			break;

		fp = next_fp;
	}

	printf("\n");
}

/* The stacks the kernel runs on, besides the per-CPU pairs. */
extern char boot_stack[], boot_stack_top[];	/* head.S */
extern char k_overflow_stacks[];		/* vectors.S */

/*===========================================================================*
 *				kernel_stack_bounds			     *
 *===========================================================================*/
/*
 * The stack an address lies on, as [lo, hi): this CPU's half of its pair,
 * its emergency stack, or the boot stack in head.S. Zero for anything else.
 *
 * A walk of the kernel's own chain still reads raw memory - the chain is the
 * kernel's, and data_copy() is for a process's - so this is what keeps it
 * from wandering: every record has to lie inside the stack the walk started
 * on, and the guard page under each CPU's stack, which is not mapped, is
 * below lo.
 */
static int
kernel_stack_bounds(vir_bytes a, vir_bytes *lo, vir_bytes *hi)
{
	vir_bytes base;

	if (a >= (vir_bytes)k_stacks_start && a < (vir_bytes)k_stacks_end) {
		base = a & ~(vir_bytes)(2 * K_STACK_SIZE - 1);
		*lo = base + K_STACK_SIZE;
		*hi = base + 2 * K_STACK_SIZE;
	} else if (a >= (vir_bytes)k_overflow_stacks && a <
	    (vir_bytes)k_overflow_stacks + K_OVERFLOW_STACK_SIZE *
	    CONFIG_MAX_CPUS) {
		*lo = a & ~(vir_bytes)(K_OVERFLOW_STACK_SIZE - 1);
		*hi = *lo + K_OVERFLOW_STACK_SIZE;
	} else if (a >= (vir_bytes)boot_stack && a < (vir_bytes)boot_stack_top) {
		*lo = (vir_bytes)boot_stack;
		*hi = (vir_bytes)boot_stack_top;
	} else
		return 0;

	return a >= *lo;
}

/*===========================================================================*
 *				util_stacktrace				     *
 *===========================================================================*/
/*
 * The kernel's own stack, as return addresses, innermost first: what panic()
 * prints after "kernel on CPU n:". The kernel's version of the libsys one,
 * which it replaces at link time, because that one follows the chain
 * wherever it points.
 *
 * It had nothing to follow until 2026-10-14: the kernel was built with
 * -fomit-frame-pointer like the rest of the system, x29 held whatever the
 * last function left in it, and a panic printed an empty line. Now the
 * kernel keeps frame records (arch/aarch64/Makefile.inc), the entry from
 * EL0 zeroes x29 so the chain ends at trap_handler(), and an exception taken
 * in the kernel adds a record holding the faulting pc (vectors.S), so the
 * trace runs on through it into the code that faulted. Functions from the
 * libraries linked in (libminc, libsys, libexec, libtimers) keep no record:
 * their callers' return addresses are missing from the trace, and nothing
 * else is wrong with it.
 *
 * Addresses only. port/kstack.sh turns a console line back into names.
 */
#define KSTACKTRACE_MAX_FRAMES	32

void
util_stacktrace(void)
{
	vir_bytes fp = (vir_bytes)__builtin_frame_address(0);
	vir_bytes lo, hi, next;
	int i;

	if (!kernel_stack_bounds(fp, &lo, &hi)) {
		printf("(fp 0x%lx is on no kernel stack)\n", (unsigned long)fp);
		return;
	}

	for (i = 0; i < KSTACKTRACE_MAX_FRAMES; i++) {
		if (fp < lo || fp + 2 * sizeof(reg_t) > hi ||
		    (fp % sizeof(reg_t)) != 0)
			break;

		next = ((vir_bytes *)fp)[0];
		printf("0x%lx ", (unsigned long)((reg_t *)fp)[1]);

		/* As for a process: a chain goes up, or it is not one. */
		if (next <= fp)
			break;
		fp = next;
	}

	printf("\n");
}

#if PANICPROBE
/*===========================================================================*
 *				panic_probe				     *
 *===========================================================================*/
/*
 * A panic on purpose (debug.h, PANICPROBE), three calls deep so that the
 * trace has something to show: probe_a, probe_b and probe_c must all be in
 * it. Each does something after its call, so that none of them is a tail
 * call and the frames really are there.
 */
static int probe_mode = -1;
static volatile int probe_sink;

/*
 * The mode when the command line names none. The board's boot script takes
 * no arguments of one's own, so a kernel for it carries its mode in:
 * DBG="-O2 -DPANICPROBE=1 -DPANICPROBE_MODE=2".
 */
#ifndef PANICPROBE_MODE
#define PANICPROBE_MODE	0
#endif

static void __attribute__((noinline))
probe_c(int mode)
{
	/* Mode 2: the upper half here maps nothing - a kernel exception. */
	if (mode == 2)
		probe_sink = *(volatile int *)0xffff800000000000UL;
	panic("panic probe, mode %d", mode);
}

static void __attribute__((noinline))
probe_b(int mode)
{
	probe_c(mode);
	probe_sink++;
}

static void __attribute__((noinline))
probe_a(int mode)
{
	probe_b(mode);
	probe_sink++;
}

void
panic_probe(void)
{
	char *v;

	if (probe_mode < 0)
		probe_mode = (v = env_get("panicprobe")) != NULL ? atoi(v) :
		    PANICPROBE_MODE;
	if (probe_mode == 0 || get_monotonic() < 20 * system_hz)
		return;

	printf("panic probe: mode %d on cpu %u\n", probe_mode, cpuid);
	probe_a(probe_mode);
}
#endif /* PANICPROBE */

#else /* !USE_SYSDEBUG */

void
proc_stacktrace(struct proc *whichproc)
{
}

void
util_stacktrace(void)
{
}

#endif /* USE_SYSDEBUG */
