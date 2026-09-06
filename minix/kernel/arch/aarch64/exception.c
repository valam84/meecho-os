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

#else /* !USE_SYSDEBUG */

void
proc_stacktrace(struct proc *whichproc)
{
}

#endif /* USE_SYSDEBUG */
