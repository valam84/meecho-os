/*
 * The system counter, and the two things the generic kernel asks of it that
 * do not need a timer interrupt: how busy this core has been, and a
 * timestamp to stir into the randomness pool.
 *
 * The rest of this file - intr_init, the local timer, cycles accounting - is
 * the interrupt group and arrives with it. What is here now stands on its
 * own because CNTVCT_EL0 is a counter, not a device: it is running before
 * anything configures it, and reading it needs no driver.
 *
 * That is the one real difference from ARM, where read_tsc_64() lives in the
 * BSP (bsp/ti/omap_timer.c) because the counter is a board peripheral at a
 * board-specific address. Here it is an architectural register with an
 * architectural frequency in CNTFRQ_EL0, identical on every core, which is
 * also why stage 1 chose it over the PMU cycle counter for libsys.
 */

#include "kernel/kernel.h"
#include "kernel/proc.h"
#include "kernel/clock.h"
#include "kernel/glo.h"

#include <assert.h>
#include <minix/u64.h>

#include <sys/sched.h>		/* CP_*, CPUSTATES */
#if CPUSTATES != MINIX_CPUSTATES
/* If this breaks, the accounting below has to be adapted accordingly. */
#error "MINIX_CPUSTATES value is out of sync with NetBSD's!"
#endif

#include <machine/vm.h>

#include "archconst.h"
#include "arch_proto.h"

#include "bsp_timer.h"

/*
 * How the system counter relates to the units the rest of the kernel counts
 * in. Filled in by cycles_accounting_init().
 *
 * ARM hardcodes these per board - 16250 counts per millisecond on a
 * BeagleBoard-xM, 15000 on a BeagleBone, and a panic on anything else -
 * because its counter is a board peripheral whose rate nothing reports. Here
 * the rate is in CNTFRQ_EL0, so there is nothing to know about the board and
 * nothing to get wrong when a new one appears.
 */
static unsigned tsc_per_ms[CONFIG_MAX_CPUS];
static unsigned tsc_per_tick[CONFIG_MAX_CPUS];
static uint64_t tsc_per_state[CONFIG_MAX_CPUS][CPUSTATES];

/*===========================================================================*
 *				read_tsc_64				     *
 *===========================================================================*/
void
read_tsc_64(u64_t *t)
{
	u64_t v;

	/*
	 * The isb is what makes this a measurement rather than an estimate:
	 * without it the read of CNTVCT_EL0 may be satisfied out of order
	 * with respect to the instructions around it. The architecture is
	 * explicit that a counter read needs the barrier to be ordered.
	 */
	__asm__ volatile("isb");
	__asm__ volatile("mrs %0, cntvct_el0" : "=r"(v));

	*t = v;
}

/*===========================================================================*
 *				init_local_timer			     *
 *===========================================================================*/
int
init_local_timer(unsigned freq)
{
	/*
	 * ARM also works out its counter rate here, because there the counter
	 * is the timer peripheral and nothing reports its frequency until the
	 * driver is up. Here the rate is in CNTFRQ_EL0 and
	 * cycles_accounting_init() has already read it - which is just as
	 * well, since the generic kernel calls that first.
	 */
	bsp_timer_init(freq);

	return 0;
}

/*===========================================================================*
 *				stop_local_timer			     *
 *===========================================================================*/
void
stop_local_timer(void)
{
	bsp_timer_stop();
}

/*===========================================================================*
 *				restart_local_timer			     *
 *===========================================================================*/
void
restart_local_timer(void)
{
	/*
	 * Nothing to restart. This exists for architectures whose timer is
	 * one-shot and has to be rearmed after an idle period; the generic
	 * timer's comparator is advanced by every tick in
	 * bsp_timer_int_handler() and keeps running through idle, so there is
	 * no state to recover. ARM's is empty for the same reason.
	 */
}

/*===========================================================================*
 *			   register_local_timer_handler			     *
 *===========================================================================*/
int
register_local_timer_handler(const irq_handler_t handler)
{
	return bsp_register_timer_handler(handler);
}

/*===========================================================================*
 *			     arch_timer_int_handler			     *
 *===========================================================================*/
void
arch_timer_int_handler(void)
{
	bsp_timer_int_handler();
}

/*===========================================================================*
 *			     cycles_accounting_init			     *
 *===========================================================================*/
void
cycles_accounting_init(void)
{
	u64_t freq;
#ifdef CONFIG_SMP
	unsigned cpu = cpuid;
#else
	unsigned cpu = 0;
#endif

	/*
	 * CNTFRQ_EL0 is not hardware: it is a note the firmware leaves saying
	 * how fast the counter it started runs. A zero means nobody left the
	 * note, and every interval this kernel measures afterwards - a
	 * scheduling quantum, a process's CPU time, the load average - would
	 * be measured with a ruler of unknown length. There is no sensible
	 * default to fall back to, so say so here rather than mis-schedule
	 * everything from now on.
	 */
	__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
	if (freq == 0)
		panic("CNTFRQ_EL0 is zero: firmware did not set the timer "
		    "frequency");

	tsc_per_ms[cpu] = (unsigned)(freq / 1000);
	tsc_per_tick[cpu] = (unsigned)(freq / system_hz);

	read_tsc_64(get_cpu_var_ptr(cpu, tsc_ctr_switch));

	get_cpu_var(cpu, cpu_last_tsc) = 0;
	get_cpu_var(cpu, cpu_last_idle) = 0;
}

/*===========================================================================*
 *				ms_2_cpu_time				     *
 *===========================================================================*/
u64_t
ms_2_cpu_time(unsigned ms)
{
	return (u64_t)tsc_per_ms[cpuid] * ms;
}

/*===========================================================================*
 *				cpu_time_2_ms				     *
 *===========================================================================*/
unsigned
cpu_time_2_ms(u64_t cpu_time)
{
	return (unsigned)(cpu_time / tsc_per_ms[cpuid]);
}

/*===========================================================================*
 *				get_cpu_ticks				     *
 *===========================================================================*/
void
get_cpu_ticks(unsigned int cpu, uint64_t ticks[CPUSTATES])
{
	int i;

	/* TODO: make this inter-CPU safe! */
	for (i = 0; i < CPUSTATES; i++) {
		ticks[i] = tsc_per_tick[cpu] ?
		    tsc_per_state[cpu][i] / tsc_per_tick[cpu] : 0;
	}
}

/*===========================================================================*
 *				context_stop				     *
 *===========================================================================*/
void
context_stop(struct proc *p)
{
	/*
	 * Charge everything since the last switch to p, and start a new
	 * interval. Called on every crossing between a process and the
	 * kernel, so this is the single place that says where the machine's
	 * time went.
	 */
	u64_t tsc, tsc_delta;
	u64_t *__tsc_ctr_switch = get_cpulocal_var_ptr(tsc_ctr_switch);
	unsigned int cpu, tpt, counter;

#ifdef CONFIG_SMP
#error CONFIG_SMP is unsupported on aarch64
#else
	read_tsc_64(&tsc);
	p->p_cycles = p->p_cycles + tsc - *__tsc_ctr_switch;
	cpu = 0;
#endif

	tsc_delta = tsc - *__tsc_ctr_switch;

	if (kbill_ipc) {
		kbill_ipc->p_kipc_cycles += tsc_delta;
		kbill_ipc = NULL;
	}

	if (kbill_kcall) {
		kbill_kcall->p_kcall_cycles += tsc_delta;
		kbill_kcall = NULL;
	}

	/*
	 * CPU average accounting happens here rather than in the generic
	 * clock handler, so that time spent in the kernel is counted and so
	 * that a process with a lot of short activity - one that burns real
	 * CPU but is never the one running when a tick arrives - is counted
	 * too. The loop is a loop because a tick's worth of counts may have
	 * accumulated more than once; in practice it runs zero or one times.
	 */
	tpt = tsc_per_tick[cpu];

	p->p_tick_cycles += tsc_delta;
	while (tpt > 0 && p->p_tick_cycles >= tpt) {
		p->p_tick_cycles -= tpt;
		cpuavg_increment(&p->p_cpuavg, kclockinfo.uptime, system_hz);
	}

	/*
	 * Take the cycles just spent out of what is left of this process's
	 * quantum. The kernel tasks have no quantum to spend, but their time
	 * is still counted for the machine as a whole.
	 */
	if (p->p_endpoint >= 0) {
		/* On MINIX3, the "system" counter covers system processes. */
		if (p->p_priv != priv_addr(USER_PRIV_ID))
			counter = CP_SYS;
		else if (p->p_misc_flags & MF_NICED)
			counter = CP_NICE;
		else
			counter = CP_USER;

#if DEBUG_RACE
		p->p_cpu_time_left = 0;
#else
		if (tsc_delta < p->p_cpu_time_left)
			p->p_cpu_time_left -= tsc_delta;
		else
			p->p_cpu_time_left = 0;
#endif
	} else {
		/* On MINIX3, the "interrupts" counter covers the kernel. */
		if (p->p_endpoint == IDLE)
			counter = CP_IDLE;
		else
			counter = CP_INTR;
	}

	tsc_per_state[cpu][counter] += tsc_delta;

	*__tsc_ctr_switch = tsc;
}

/*===========================================================================*
 *				cpu_load				     *
 *===========================================================================*/
short
cpu_load(void)
{
	/*
	 * Percentage of the time since the last call that was not spent in
	 * the idle process. Read by the scheduler through
	 * m_krn_lsys_schedule.acnt_cpu_load when a process runs out of
	 * quantum.
	 */
	u64_t current_tsc, *current_idle;
	u64_t tsc_delta, idle_delta, busy;
	u64_t *last_tsc, *last_idle;
	struct proc *idle;
	short load;
#ifdef CONFIG_SMP
	unsigned cpu = cpuid;
#endif

	last_tsc = get_cpu_var_ptr(cpu, cpu_last_tsc);
	last_idle = get_cpu_var_ptr(cpu, cpu_last_idle);

	idle = get_cpu_var_ptr(cpu, idle_proc);
	read_tsc_64(&current_tsc);
	current_idle = &idle->p_cycles;

	if (*last_tsc) {
		tsc_delta = current_tsc - *last_tsc;
		idle_delta = *current_idle - *last_idle;

		busy = tsc_delta - idle_delta;
		busy = busy * 100;
		load = ex64lo(busy / tsc_delta);

		if (load > 100)
			load = 100;
	} else {
		/* First call: no interval to measure over yet. */
		load = 0;
	}

	*last_tsc = current_tsc;
	*last_idle = *current_idle;

	return load;
}

/*===========================================================================*
 *				get_randomness				     *
 *===========================================================================*/
void
get_randomness(struct k_randomness *rand, int source)
{
	/*
	 * Called from the generic interrupt handler for every interrupt that
	 * is forwarded to a driver, to timestamp the event for /dev/random.
	 * What is random about it is not the counter - that is a clock - but
	 * when the interrupt arrived relative to it: device jitter, measured
	 * with the finest ruler the machine has.
	 *
	 * ARM leaves this empty, so /dev/random on that port is fed by
	 * nothing at all. It is a dozen lines and the counter is already
	 * being read next door, so it is written here rather than inherited
	 * as a stub. It is a source of entropy, not a generator: the quality
	 * judgement belongs to whoever consumes the pool.
	 */
	struct k_randomness_bin *bin;
	u64_t tsc;
	int nr;

	if (source < 0)
		return;

	nr = source % RANDOM_SOURCES;
	bin = &rand->bin[nr];

	read_tsc_64(&tsc);

	/*
	 * The low bits only: the high ones move predictably with time and
	 * would just dilute the buffer. rand_t is 16 bits wide.
	 */
	bin->r_buf[bin->r_next] = (rand_t)tsc;

	if (bin->r_size < RANDOM_ELEMENTS)
		bin->r_size++;

	bin->r_next = (bin->r_next + 1) % RANDOM_ELEMENTS;
}
