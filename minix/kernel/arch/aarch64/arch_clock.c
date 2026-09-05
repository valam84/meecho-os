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

#include <assert.h>
#include <minix/u64.h>

#include <machine/vm.h>

#include "archconst.h"
#include "arch_proto.h"

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
