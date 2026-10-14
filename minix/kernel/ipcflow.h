/*
 * ipcflow, kernel side: the matrix of who sends messages to whom.  What is
 * counted and why is in <minix/ipcflow.h>.
 *
 * Unlike KTRACE this is on in an ordinary build; IPCFLOW in debug.h turns
 * it off, and exists so that its price can be measured by building both
 * ways.  Off, the macros compile to nothing.
 *
 * Include after kernel/debug.h.
 */

#ifndef KERNEL_IPCFLOW_H
#define KERNEL_IPCFLOW_H

#include "kernel/debug.h"

struct proc;

#if IPCFLOW

#include <minix/ipcflow.h>

extern uint64_t ipcflow_cell[IF_NPROC][IF_NPROC + 1];
extern uint64_t ipcflow_kernel[IF_NPROC];

/*
 * A message from src to dst.  p_nr + NR_TASKS is the slot, kernel tasks
 * (negative p_nr) first: an index without a division, which pointer
 * subtraction from the process table would be.
 */
#define IPCFLOW_COUNT(src, dst)	do {					\
	ipcflow_cell[(src)->p_nr + NR_TASKS][(dst)->p_nr + NR_TASKS]++;	\
} while (0)

/* The same message, sent by the kernel in src's name. */
#define IPCFLOW_COUNT_KERNEL(src, dst)	do {				\
	IPCFLOW_COUNT(src, dst);					\
	ipcflow_kernel[(src)->p_nr + NR_TASKS]++;			\
} while (0)

void ipcflow_clear(const struct proc *rc);
int ipcflow_row(endpoint_t e, struct ipcflow *row);

#else /* !IPCFLOW */

#define IPCFLOW_COUNT(src, dst)		do { } while (0)
#define IPCFLOW_COUNT_KERNEL(src, dst)	do { } while (0)
#define ipcflow_clear(rc)		do { } while (0)

#endif /* IPCFLOW */

#endif /* KERNEL_IPCFLOW_H */
