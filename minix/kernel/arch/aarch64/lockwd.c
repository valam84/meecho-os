/*
 * The lock watchdog: a core that has waited too long for the big kernel lock
 * says who holds it, and stops the machine.
 *
 * What it is for. The kernel runs with interrupts masked and under one lock.
 * A core that hangs inside it - a spin on a condition nobody will set, a wfe
 * without its event, a loop that does not end - hangs with the lock held, and
 * its own timer is exactly what it cannot hear. Before this the result was
 * silence: no panic, no message, and on the board nothing until bootwd, which
 * after boot is off.
 *
 * Who can tell. Not the hung core, but every other one: the next time any of
 * them enters the kernel - a system call, a fault, its own timer tick - it
 * waits for the lock in context_stop(), and that wait is where the hang shows.
 * So the watch is on the wait. A waiter that gives up after LOCKWD seconds
 * prints what the holder was doing when it took the lock, and panics; the
 * panic stops the other cores and resets the machine, and the report is on
 * the console instead of nowhere.
 *
 * Two things make "every other core" true rather than hopeful:
 *
 * - The wait cannot sleep in plain wfe, which a dead holder would never
 *   end. arch_spinlock_lock_until() looks at the counter each time it wakes,
 *   and the timer's event stream, turned on here for every core, wakes it
 *   about once a millisecond.
 *
 * - An idle secondary stops its timer (the boot core keeps time), and a core
 *   that never enters the kernel never waits. With the boot core hung and
 *   the rest idle, nobody would notice. So an idle secondary, instead of
 *   stopping its timer, sets it to wake once in half a watchdog period and
 *   try the lock. That is one interrupt per core per five seconds.
 *
 * What it does not cover, and cannot in software: a machine with one core,
 * or one where every core is hung. Nothing is left running to notice. That
 * needs the SoC's own hardware watchdog, kicked from the tick, which is a
 * device driver and not part of this. User-level starvation - the kernel
 * alive, one process taking all of it - is not a held lock either; bootwd is
 * what covers that during boot.
 *
 * How long. Ten seconds by default, "lockwd=N" on the command line, 0 to turn
 * it off. The kernel lock is held for microseconds; the longest legitimate
 * holds are boot - which this does not watch, the secondaries wait for the
 * lock there with plain BKL_LOCK() - and a debugging dump printed with
 * do_serial_debug, which at 115200 can run for seconds. Ten is well clear of
 * both and still short of anybody's patience.
 */

#include <stdlib.h>

#include "kernel/kernel.h"
#include "kernel/proc.h"
#include "kernel/glo.h"
#include "kernel/clock.h"
#include "kernel/smp.h"
#include "kernel/spinlock.h"

#include "archconst.h"
#include "arch_proto.h"
#include "bsp_timer.h"

#define LOCKWDVARNAME		"lockwd"
#define LOCKWD_DEFAULT_S	10

/* How long a wait may last, in counter ticks; 0 means the watchdog is off. */
static u64_t lockwd_ticks;
static unsigned lockwd_seconds;

/* Taken by the one waiter that reports; the others go on waiting. */
static atomic_t lockwd_fired;

/*
 * Who holds the kernel lock, as far as context_stop() knows: the core, when
 * it took the lock, and the process it entered the kernel for. Set after the
 * lock is taken and cleared before it is let go, so only the holder writes
 * them. The generic SMP code also takes the lock in a few places of its own
 * (boot, waiting for a core); a hold like that shows as holder -1.
 */
static volatile int bkl_holder = -1;
static volatile u64_t bkl_since;
static struct proc * volatile bkl_for;

int arch_spinlock_lock_until(atomic_t *lock, u64_t deadline);

/*===========================================================================*
 *				lockwd_cpu_init				     *
 *===========================================================================*/
void
lockwd_cpu_init(void)
{
	u64_t freq, cntkctl, evnti;
	char *value;

	__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));

	/*
	 * The boot core decides, once, before any secondary exists. arch_init()
	 * runs at the end of cstart(), where the command line is parsed.
	 */
	if (cpuid == 0) {
		lockwd_seconds = LOCKWD_DEFAULT_S;
		if ((value = env_get(LOCKWDVARNAME)) != NULL)
			lockwd_seconds = (unsigned)atoi(value);
		lockwd_ticks = freq * lockwd_seconds;
	}

	if (lockwd_ticks == 0 || freq == 0)
		return;

	/*
	 * The event stream: an event each time bit EVNTI of the virtual
	 * counter goes from 0 to 1, that is every 2^(EVNTI+1) ticks. Aim for
	 * about a millisecond whatever the counter's rate - 0.5 ms at QEMU's
	 * 62.5 MHz, 0.7 ms at the CB2's 24 MHz. Only a core waiting in wfe
	 * notices it at all.
	 */
	freq /= 1000;
	for (evnti = 0; evnti < 15 && (2ULL << (evnti + 1)) <= freq; evnti++)
		;

	__asm__ volatile("mrs %0, cntkctl_el1" : "=r"(cntkctl));
	cntkctl &= ~(AARCH64_CNTKCTL_EVNTI_MASK | AARCH64_CNTKCTL_EVNTDIR);
	cntkctl |= AARCH64_CNTKCTL_EVNTEN |
	    (evnti << AARCH64_CNTKCTL_EVNTI_SHIFT);
	__asm__ volatile("msr cntkctl_el1, %0" :: "r"(cntkctl));
	__asm__ volatile("isb");
}

/*===========================================================================*
 *				lockwd_expired				     *
 *===========================================================================*/
static void
lockwd_expired(u64_t waited, struct proc *p)
{
	int holder = bkl_holder;
	struct proc *for_p = bkl_for;
	struct proc *running;
	u64_t now;

	/*
	 * A panic in progress has let go of the lock on purpose and is now
	 * printing, which can take longer than the watchdog allows. Not a
	 * hang; keep waiting, and the panic will stop this core.
	 */
	if (kinfo.minix_panicing)
		return;

	/* One report per machine: a second one would reset in the first. */
	if (arch_spinlock_lock_until(&lockwd_fired, 0) != 0)
		return;

	read_tsc_64(&now);

	printf("\nlock watchdog: cpu %u has waited %u ms for the kernel lock, "
	    "entering for %s / %d\n", cpuid, cpu_time_2_ms(waited),
	    p->p_name, p->p_endpoint);

	if (holder < 0 || holder >= (int)ncpus) {
		printf("the lock was taken outside context_stop()\n");
	} else {
		running = get_cpu_var(holder, proc_ptr);
		printf("held by cpu %d for %u ms; it entered the kernel for "
		    "%s / %d, and %s / %d is its current process\n",
		    holder, cpu_time_2_ms(now - bkl_since),
		    for_p ? for_p->p_name : "?", for_p ? for_p->p_endpoint : 0,
		    running ? running->p_name : "?",
		    running ? running->p_endpoint : 0);

		/*
		 * Where that process was when it entered: the one thing about
		 * the hung core that can be read without its help. The kernel
		 * side of it - where the core is now - cannot; that needs the
		 * core's external debug interface, which is not mapped.
		 */
		if (for_p != NULL && !isemptyp(for_p))
			proc_stacktrace(for_p);
	}

	panic("kernel lock held for over %u s", lockwd_seconds);
}

/*===========================================================================*
 *				bkl_lock_watched			     *
 *===========================================================================*/
void
bkl_lock_watched(struct proc *p)
{
	u64_t start, now;

	if (lockwd_ticks == 0) {
		BKL_LOCK();
	} else {
		read_tsc_64(&start);
		now = start;
		while (arch_spinlock_lock_until(&big_kernel_lock.val,
		    now + lockwd_ticks) != 0) {
			read_tsc_64(&now);
			lockwd_expired(now - start, p);
			/* Somebody else reported, or a panic is under way. */
		}
	}

	read_tsc_64(&now);
	bkl_since = now;
	bkl_holder = (int)cpuid;
	bkl_for = p;
}

/*===========================================================================*
 *				bkl_unlock_watched			     *
 *===========================================================================*/
void
bkl_unlock_watched(void)
{
	bkl_holder = -1;
	bkl_for = NULL;
	BKL_UNLOCK();
}

/*===========================================================================*
 *				stop_local_timer_idle			     *
 *===========================================================================*/
void
stop_local_timer_idle(void)
{
	/*
	 * Called by idle() on a secondary in place of stop_local_timer(). Half
	 * the period, so that a hang is noticed within one and a half periods
	 * even when every other core is idle.
	 */
	if (lockwd_ticks == 0)
		stop_local_timer();
	else
		bsp_timer_idle_wake(lockwd_ticks / 2);
}
