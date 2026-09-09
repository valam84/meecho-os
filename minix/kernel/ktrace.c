/*
 * ktrace: the counters themselves.  See <minix/ktrace.h> for why they are
 * plain globals and not per-CPU.
 */

#include "kernel/kernel.h"
#include "kernel/proc.h"
#include "kernel/proto.h"
#include "kernel/ktrace.h"

#if KTRACE

struct ktrace ktrace;

/*
 * Which way in is being served.  Not per-CPU for the same reason the
 * counters are not: the big kernel lock means one core is inside at a time,
 * and this variable lives exactly between context_stop(p) on the way in and
 * context_stop(KERNEL) on the way out - that is, entirely inside the lock.
 */
unsigned ktrace_class = KTE_OTHER;

/*===========================================================================*
 *				ktrace_charge				     *
 *===========================================================================*/
/*
 * Charge one crossing to the process that caused it.  The index is the one
 * the process table uses, so kernel tasks land in the first NR_TASKS slots
 * and a slot that has been reused by a later process shares its row - which
 * is what the process table does too, and the reader is told the name from
 * the same table.
 */
void
ktrace_charge(struct proc *p, int column)
{
	int i;

	if (p == NULL)
		return;
	i = (int)(p - BEG_PROC_ADDR);
	if (i < 0 || i >= KT_NPROC)
		return;
	ktrace.kt_proc[i][column]++;
}

#endif /* KTRACE */
