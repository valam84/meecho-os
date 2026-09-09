/*	$NetBSD$	*/

/*
 * ktrace - counters for what the kernel is actually doing.
 *
 * Why this exists.  On a microkernel the interesting question about a
 * workload is not how fast it computes but how much of its time is spent
 * crossing into the kernel and back, and the machine already answers half of
 * that: kern.cp_time says how much time went to user code, to system
 * processes and to the kernel, and psinfo says the same per process, split
 * into own time, IPC and kernel calls.  What neither says is *how many* of
 * anything there were.  A share without a count cannot be divided into a
 * price and a quantity, and until it is, "IPC is expensive" is an opinion.
 *
 * So this counts.  Every way into the kernel, every IPC primitive, every
 * kernel call by number, the events that a scheduler or an address space
 * switch consists of, and - because the same count means different things
 * for VFS and for a shell - who caused each one.  Alongside each way in, the
 * cycles spent in the kernel before leaving through it; that division is
 * free, because context_stop() already computes exactly that number for its
 * own accounting.
 *
 * The counters are plain, non-atomic and not per-CPU, and that is correct
 * rather than sloppy: everything counted here runs under the big kernel
 * lock, which context_stop() takes on the way in and releases on the way
 * out, so no two cores are ever inside these increments at once.
 *
 * Compiled in only when KTRACE is set in kernel/debug.h; GET_KTRACE answers
 * EINVAL otherwise, and /proc/ktrace says so in one line.  It is not free -
 * a handful of increments on every kernel entry - and the cost of the
 * instrument is measured by building both ways, not assumed.
 */

#ifndef _MINIX_KTRACE_H
#define _MINIX_KTRACE_H

#include <minix/config.h>
#include <minix/com.h>
#include <minix/ipcconst.h>
#include <minix/type.h>
#include <stdint.h>

/* Ways into the kernel.  One entry, one exit, one of these. */
#define KTE_IPC		0	/* svc #IPCVEC_INTR: send, receive, ... */
#define KTE_KCALL	1	/* svc #KERVEC_INTR: a kernel call */
#define KTE_IRQ_USER	2	/* a device interrupt taken from a process */
#define KTE_IRQ_IDLE	3	/* a device interrupt that ended a halt */
#define KTE_FAULT	4	/* a page fault or other abort */
#define KTE_FPU		5	/* first FP instruction after a switch */
#define KTE_OTHER	6	/* anything else, and the resting state */
#define KT_NENTRY	7

#define KT_ENTRY_NAMES { "ipc", "kcall", "irq", "irq_idle", "fault", \
	"fpu", "other" }

/* Events inside the kernel, counted where they happen. */
#define KTV_CTXSW	0	/* switch_to_user picked a different process */
#define KTV_ASIDSW	1	/* TTBR0 actually rewritten */
#define KTV_TLBFLUSH	2	/* a TLB invalidate on a switch */
#define KTV_MSGIN	3	/* copy_msg_from_user */
#define KTV_MSGOUT	4	/* copy_msg_to_user */
#define KTV_NOQUANTUM	5	/* SCHEDULING_NO_QUANTUM sent to a scheduler */
#define KTV_IDLE	6	/* the core found nothing to run */
#define KTV_ENQUEUE	7	/* a process put on a run queue */
#define KTV_DEQUEUE	8	/* a process taken off one */
#define KTV_PICKPROC	9	/* the run queues scanned for a process */
#define KTV_SENDBLOCK	10	/* a send that had to block */
#define KTV_RECVBLOCK	11	/* a receive that had to block */
#define KTV_NOTIFY	12	/* mini_notify delivered or marked pending */
#define KTV_SIG		13	/* a signal caused */
#define KT_NEV		14

#define KT_EV_NAMES { "ctxsw", "asidsw", "tlbflush", "msgin", "msgout", \
	"noquantum", "idle", "enqueue", "dequeue", "pickproc", "sendblock", \
	"recvblock", "notify", "sig" }

#define KT_NIPC		(IPCNO_HIGHEST + 2)	/* 0 is unused, plus SENDA */
#define KT_NKCALL	NR_SYS_CALLS
#define KT_NPROC	(NR_TASKS + NR_PROCS)
#define KT_NAMELEN	PROC_NAME_LEN

#define KTRACE_VERSION	1

struct ktrace {
	uint64_t kt_version;
	uint64_t kt_freq;	/* CNTFRQ_EL0: what the cycles below are */
	uint64_t kt_tsc;	/* the counter now, so a reader can time its
				 * own window without asking twice */

	uint64_t kt_entry[KT_NENTRY];		/* how many */
	uint64_t kt_entry_cycles[KT_NENTRY];	/* and how long they stayed */

	uint64_t kt_ipc[KT_NIPC];	/* by IPC primitive */
	uint64_t kt_kcall[KT_NKCALL];	/* by kernel call number */
	uint64_t kt_ev[KT_NEV];		/* by event */

	/*
	 * Who.  Indexed the way the process table is, by proc_nr_t + NR_TASKS,
	 * so the kernel tasks come first.  Two columns rather than one because
	 * the same count means a different thing for a shell that asks for a
	 * kernel call and for VFS that answers an IPC.
	 */
	uint64_t kt_proc[KT_NPROC][2];

	/*
	 * The names for those rows, filled in when the counters are read
	 * rather than when they are counted: charging a crossing has to be
	 * cheap, and the process table has the name anyway.  A row whose
	 * slot has since been reused carries the new tenant's name, which is
	 * the same thing /proc says about a recycled slot.
	 */
	char kt_name[KT_NPROC][KT_NAMELEN];
};

#define KT_PROC_IPC	0
#define KT_PROC_KCALL	1

#endif /* _MINIX_KTRACE_H */
