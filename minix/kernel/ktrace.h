/*
 * ktrace, kernel side: the macros the counted places use.
 *
 * All of them compile to nothing when KTRACE is 0 in debug.h, so the
 * instrumented places read the same either way and the instrument can be
 * left in the source.  What it costs is measured by building both ways -
 * see port/PORTING-LOG.md, "Из чего складывается IPC".
 *
 * Include after kernel/debug.h.
 */

#ifndef KERNEL_KTRACE_H
#define KERNEL_KTRACE_H

#include "kernel/debug.h"

struct proc;

#if KTRACE

#include <minix/ktrace.h>

extern struct ktrace ktrace;
extern unsigned ktrace_class;

void ktrace_charge(struct proc *p, int column);

/*
 * A way into the kernel.  Recorded rather than counted on the way out,
 * because the way out does not know what the way in was; the cycles are
 * added by KTRACE_LEAVE(), which context_stop() calls with the number it
 * has already computed for its own accounting.
 */
#define KTRACE_ENTER(c)		do {					\
	ktrace_class = (c);						\
	ktrace.kt_entry[c]++;						\
} while (0)

#define KTRACE_LEAVE(cycles)	do {					\
	ktrace.kt_entry_cycles[ktrace_class] += (cycles);		\
	ktrace_class = KTE_OTHER;					\
} while (0)

#define KTRACE_EV(e)		do { ktrace.kt_ev[e]++; } while (0)
#define KTRACE_EV_IF(c, e)	do { if (c) ktrace.kt_ev[e]++; } while (0)
#define KTRACE_IPC(n)		do {					\
	if ((unsigned)(n) < KT_NIPC) ktrace.kt_ipc[n]++;		\
} while (0)
#define KTRACE_KCALL(n)		do {					\
	if ((unsigned)(n) < KT_NKCALL) ktrace.kt_kcall[n]++;		\
} while (0)
#define KTRACE_CHARGE(p, col)	ktrace_charge(p, col)

#else /* !KTRACE */

#define KTRACE_ENTER(c)		do { } while (0)
#define KTRACE_LEAVE(cycles)	do { } while (0)
#define KTRACE_EV(e)		do { } while (0)
/* The condition is still evaluated away, so its operands stay used. */
#define KTRACE_EV_IF(c, e)	do { (void)(c); } while (0)
#define KTRACE_IPC(n)		do { } while (0)
#define KTRACE_KCALL(n)		do { } while (0)
#define KTRACE_CHARGE(p, col)	do { } while (0)

#endif /* KTRACE */

#endif /* KERNEL_KTRACE_H */
