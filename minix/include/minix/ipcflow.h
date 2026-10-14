/*
 * ipcflow - who sends messages to whom: the kernel's message matrix.
 *
 * Why this exists.  On a microkernel the system is a graph of processes,
 * and the edges of that graph are messages.  A program that writes a file
 * talks to VFS, VFS to MFS, MFS to the disk driver; a slow window is slow
 * somewhere along such a path.  KTRACE (<minix/ktrace.h>) counts how often
 * each process enters the kernel, but not whom it talks to, and it is a
 * build-time instrument; the desktop's x-ray view (stage 11.7) needs the
 * edges, all the time.  So the kernel keeps a matrix: a cell per pair of
 * process table slots, counting the messages the first sent to the second.
 *
 * What is a message here.  One cell is incremented where a message
 * actually leaves its sender, not where the sender asked for it:
 *
 *   - a send (SEND, SENDNB, the send half of SENDREC) once it has been
 *     copied - delivered at once or queued at the receiver;
 *   - a notification, delivered or marked pending;
 *   - an asynchronous message (SENDA) once its delivery is recorded in the
 *     sender's table - that is, exactly once however often it was tried;
 *   - a kernel call, as a message to SYSTEM, which is what it is.
 *
 * A failed send is not a message, and neither is a receive.  The kernel
 * tasks are rows too - HARDWARE notifies drivers of interrupts, CLOCK of
 * alarms, SYSTEM of signals, ASYNCM of finished asynchronous sends.
 *
 * Messages the kernel sends in a process's name - a page fault reported to
 * VM, the end of a quantum reported to the scheduler - carry that process
 * as their source, and are counted in its row like any other; how many of
 * its row they are is kept beside it (if_kernel), because the process
 * itself did not ask for them.
 *
 * Slots are reused.  When a process goes, the kernel moves what was sent
 * to it into each sender's "gone" column and clears its row, so that a
 * row always describes the slot's present tenant and every row's sum only
 * ever grows.
 *
 * The cells are plain and not per-CPU, for the reason the KTRACE counters
 * are: every place that counts runs under the big kernel lock.  The matrix
 * is compiled in unless IPCFLOW is 0 in kernel/debug.h; GET_IPCFLOW
 * answers EINVAL then.  Its price is measured, not assumed - see
 * port/PORTING-LOG.md, "Рентген, часть первая".
 */

#ifndef _MINIX_IPCFLOW_H
#define _MINIX_IPCFLOW_H

#include <minix/config.h>
#include <minix/com.h>
#include <minix/type.h>
#include <stdint.h>

#define IF_NPROC	(NR_TASKS + NR_PROCS)	/* rows and columns: the
						 * process table, kernel tasks
						 * first, as KTRACE has it */
#define IF_GONE		IF_NPROC	/* the column of processes gone */
#define IF_NAMELEN	PROC_NAME_LEN

#define IPCFLOW_VERSION	1

/*
 * One row, as sys_getinfo(GET_IPCFLOW) hands it out: the messages one
 * process has sent, by receiver.  Asked for by endpoint, so that a row is
 * never handed out for a tenant other than the one asked about.
 */
struct ipcflow {
	uint64_t if_version;
	uint64_t if_freq;	/* what if_tsc counts per second */
	uint64_t if_tsc;	/* the counter when the row was taken */
	int32_t if_slot;	/* the row: proc_nr + NR_TASKS, as KTRACE's
				 * "proc" rows are numbered */
	int32_t if_endpoint;	/* its tenant */
	uint64_t if_kernel;	/* of the row, sent by the kernel in its name */
	/*
	 * Each column's tenant at the time the row was taken - endpoint, NONE
	 * for a free slot, and name: taken under the same lock as the counts,
	 * so that a reader need not join them with a process table fetched at
	 * another moment.
	 */
	int32_t if_dst[IF_NPROC];
	char if_name[IF_NPROC][IF_NAMELEN];
	uint64_t if_count[IF_NPROC + 1];	/* + IF_GONE */
};

#endif /* _MINIX_IPCFLOW_H */
