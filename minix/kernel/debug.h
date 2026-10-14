#ifndef DEBUG_H
#define DEBUG_H

/* This header file defines all debugging constants and macros, and declares
 * some variables. Certain debugging features redefine standard constants
 * and macros. Therefore, this header file should be included after the
 * other kernel headers.
 */

#ifndef __ASSEMBLY__
#include <minix/debug.h>
#include "config.h"
#endif

/* Debug info via serial (see ser_debug()) */
#define DEBUG_SERIAL			1

/* Enable prints such as
 *  . send/receive failed due to deadlock or dead source or dead destination
 *  . trap not allowed
 *  . bogus message pointer
 *  . kernel call number not allowed by this process
 *
 * Of course the call still fails, but nothing is printed if these warnings
 * are disabled.
 */
#define DEBUG_ENABLE_IPC_WARNINGS	1

/* Sanity checks. */
#define DEBUG_SANITYCHECKS		0

/* Verbose messages. */
#define DEBUG_TRACE			0

/* DEBUG_RACE makes every process preemptible, schedules
 * every process on the same priority queue, and randomizes
 * the next process to run, in order to help catch race
 * conditions that could otherwise be masked.
 */
#define DEBUG_RACE			0

/* DEBUG_DUMPIPC dumps all IPC to serial; due to the amount of logging it is
 * strongly recommended to set "ctty 0" in the boot monitor and run inside a
 * virtual machine if you enable this; on the hardware it would take forever
 * just to boot
 */
#define DEBUG_DUMPIPC			0

/*
 * DEBUG_BOOT_TRACE marks the steps between the memory map and the first
 * process, one line each.
 *
 * For the case the other debugging aids do not reach: a kernel that stops
 * before there is anything to attach a debugger to. On a board there is no
 * gdbstub, and the only thing that comes back is what the console already
 * printed - so the question "how far did it get" has to be answerable by the
 * output itself. Cheap enough to leave in the source and switched off: a
 * dozen lines on a boot that works.
 *
 * Set to 1, rebuild the kernel, and set it back afterwards, the same way
 * DEBUG_DUMPIPC is used.
 */
#define DEBUG_BOOT_TRACE		0

#if DEBUG_BOOT_TRACE
#define BOOT_TRACE(x)			printf x
#else
#define BOOT_TRACE(x)			((void)0)
#endif

/* DEBUG_DUMPIPCF dumps filtered IPC to serial.
 */
#define DEBUG_DUMPIPCF			0

/* If defined, restrict DEBUG_DUMPIPC to particular process names */
/* #define DEBUG_DUMPIPC_NAMES		{ "tty", "pty" } */

/* DEBUG_IPCSTATS collects information on who sends messages to whom. */
#define DEBUG_IPCSTATS			0

/*
 * KTRACE counts what the kernel does: every way in, every IPC primitive,
 * every kernel call by number, the events a context switch is made of, and
 * who caused each crossing.  Alongside each way in it accumulates the cycles
 * spent in the kernel before leaving through it, using the number
 * context_stop() computes anyway for its own accounting.
 *
 * Unlike DEBUG_DUMPIPC this is quiet and bounded - a few increments per
 * kernel entry and one page of counters - so it can be left on for a whole
 * workload and read once at the end, through /proc/ktrace.  It is still not
 * free, and the price is measured by building both ways rather than assumed;
 * see port/PORTING-LOG.md, "Из чего складывается вход в ядро".
 *
 * Set to 1, rebuild the kernel, and set it back afterwards, the same way
 * DEBUG_DUMPIPC is used.  Or leave the source alone and build a kernel of
 * one's own with DBG="-O2 -DKTRACE=1" in an object directory of its own
 * (port/kbuild-variant.sh): the tree and the kernel others boot are shared.
 */
#ifndef KTRACE
#define KTRACE				0
#endif

/*
 * IPCFLOW keeps the matrix of who sends messages to whom - a cell per pair
 * of process table slots - and hands it out a row at a time through
 * GET_IPCFLOW and /proc/<pid>/ipc; <minix/ipcflow.h> says what is counted.
 * It is the desktop's x-ray view of the system (stage 11.7), so unlike
 * KTRACE it is on in an ordinary build: one increment per message, under
 * the big kernel lock.  Zero is for measuring that price by building both
 * ways, as KTRACE's was - see port/PORTING-LOG.md, "Рентген, часть первая".
 *
 * DEBUG_IPCSTATS above is its ancestor and a different thing: counted at
 * the receiver, without notifications or kernel tasks, wiped by both row
 * and column when a slot is freed, and printed on the console.
 */
#ifndef IPCFLOW
#define IPCFLOW				1
#endif

/*
 * DEBUG_STOPRACE checks, where the kernel starts serving a process's own
 * request - an IPC call, a kernel call, a page fault - that the process is
 * still allowed to run.  On SMP a process can trap while another core holds
 * the big kernel lock, and that core can stop it (smp_schedule_stop_proc(),
 * served by smp_sched_handler() in context_stop()) before this one gets the
 * lock; serving its request then is serving a process that PM believes
 * stopped.  A line per offence - the first sixteen, then every 1024th - and
 * the same for every entry the kernel undoes instead (trap.c).
 *
 * Set to 1, rebuild the kernel, and set it back afterwards, the same way
 * DEBUG_DUMPIPC is used.  See port/PORTING-LOG.md, "Остановленный процесс
 * не входит в ядро".
 */
#define DEBUG_STOPRACE			0

#if DEBUG_STOPRACE
#define STOPRACE_CHECK(p, what, nr)	stoprace_check((p), (what), (nr))
#define STOPRACE_UNDONE(p, what, nr)	stoprace_undone((p), (what), (nr))
#else
#define STOPRACE_CHECK(p, what, nr)	((void)0)
#define STOPRACE_UNDONE(p, what, nr)	((void)0)
#endif

/*
 * PANICPROBE makes the kernel panic on purpose, to see what a panic says
 * about the kernel's own stack: with panicprobe=1 on the command line,
 * panic() three calls deep; with panicprobe=2, a load from an unmapped
 * kernel address three calls deep, which is a kernel exception and then
 * the panic. Fired from the first interrupt taken from a process twenty
 * seconds after boot, so that the system is up. aarch64 only.
 *
 * Never in an ordinary build: DBG="-O2 -DPANICPROBE=1" in an object
 * directory of its own (port/kbuild-variant.sh), as for KTRACE. The stand
 * is port/test/panic/qemu-kstack.py; see port/PORTING-LOG.md, "Стек ядра
 * в панике".
 */
#ifndef PANICPROBE
#define PANICPROBE			0
#endif

#if !USE_SYSDEBUG
#undef DEBUG_SERIAL
#undef DEBUG_ENABLE_IPC_WARNINGS
#endif

#if DEBUG_DUMPIPC || DEBUG_IPCSTATS	/* either of these needs the hook */
#define DEBUG_IPC_HOOK			1
#endif

#if DEBUG_TRACE

#define VF_SCHEDULING		(1L << 1)
#define VF_PICKPROC		(1L << 2)

#define TRACE(code, statement) if(verboseflags & code) { printf("%s:%d: ", __FILE__, __LINE__); statement }

#else
#define TRACE(code, statement)
#endif

#ifdef CONFIG_BOOT_VERBOSE
#define BOOT_VERBOSE(x)	x
#else
#define BOOT_VERBOSE(x)
#endif

#ifdef _SYSTEM
#define DEBUG_PRINT(params, level) do { \
	if (verboseboot >= (level)) printf params; } while (0)
#define DEBUGBASIC(params) DEBUG_PRINT(params, VERBOSEBOOT_BASIC)
#define DEBUGEXTRA(params) DEBUG_PRINT(params, VERBOSEBOOT_EXTRA)
#define DEBUGMAX(params)   DEBUG_PRINT(params, VERBOSEBOOT_MAX)
#endif

#endif /* DEBUG_H */
