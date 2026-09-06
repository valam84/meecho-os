/*
 * Bringing the other cores up, and talking to them once they are.
 *
 * There is no trampoline here and no real-mode anything: PSCI takes a
 * physical entry point and a context word, and the firmware starts the core
 * there. So the whole of the machine-specific part is "which cores exist"
 * and "what is each one called", and the device tree answers both.
 *
 * The shape of the rest follows i386's arch_smp.c, because the generic code
 * expects it: the boot core discovers, starts each secondary and waits for it
 * to report, then finishes booting itself; a secondary takes boot_lock and
 * the big kernel lock, sets itself up, says it is up, and goes to look for
 * work.
 */

#include <assert.h>
#include <string.h>
#include <sys/types.h>
#include <minix/type.h>

#include "kernel/kernel.h"
#include "kernel/clock.h"
#include "kernel/interrupt.h"
#include "kernel/proc.h"
#include "kernel/smp.h"

#include "arch_proto.h"
#include "fdt.h"
#include "gic.h"
#include "psci.h"

#include "bsp_intr.h"

/* Where head.S starts a secondary, so its physical address can be taken. */
extern char _start_ap[];

u64_t cpu_mpidr[CONFIG_MAX_CPUS];

/*
 * Set by a secondary once it is running in the upper half, read by the boot
 * core while it waits. Volatile because that wait is the only thing that
 * changes it and the compiler cannot see the other core writing.
 */
static volatile int ap_cpu_ready;

/* How long to wait for a core to come up, in ticks of the spin below. */
#define AP_BOOT_SPINS	10000000

/*===========================================================================*
 *				this_mpidr				     *
 *===========================================================================*/
static u64_t
this_mpidr(void)
{
	u64_t mpidr;

	__asm__ volatile("mrs %0, mpidr_el1" : "=r"(mpidr));

	return mpidr & MPIDR_AFF_MASK;
}

/*===========================================================================*
 *				find_cpus				     *
 *===========================================================================*/
/*
 * The cores, out of /cpus in the device tree.
 *
 * Each child of /cpus with device_type "cpu" is one, and its reg property is
 * the core's MPIDR affinity - the same number PSCI wants and the same one
 * MPIDR_EL1 reads back. How wide reg is comes from /cpus's own
 * #address-cells, which is 1 on a machine whose affinities fit in 32 bits and
 * 2 where Aff3 is used.
 *
 * The walk visits parents before children, so noting /cpus on the way past is
 * enough to know which depth-2 nodes are cores.
 */
struct cpu_scan {
	unsigned addr_cells;
	int in_cpus;
	unsigned count;
};

static int
find_cpus(void *cookie, int depth, const char *name,
	const struct fdt_node *node)
{
	struct cpu_scan *s = cookie;
	const void *p;
	unsigned len;

	if (depth == 1) {
		s->in_cpus = (strcmp(name, "cpus") == 0);
		if (s->in_cpus) {
			s->addr_cells = 1;
			if ((p = fdt_getprop(node, "#address-cells",
			    &len)) != NULL && len == 4)
				s->addr_cells =
				    (unsigned)fdt_read_cells(p, 1);
		}
		return 0;
	}

	if (depth != 2 || !s->in_cpus)
		return 0;

	if ((p = fdt_getprop(node, "device_type", &len)) == NULL ||
	    strcmp((const char *)p, "cpu") != 0)
		return 0;

	if ((p = fdt_getprop(node, "reg", &len)) == NULL ||
	    len < 4 * s->addr_cells)
		return 0;

	/*
	 * More cores than this kernel was built for is not an error worth
	 * stopping on: the ones that fit are still usable, and the count is a
	 * build-time choice rather than a property of the machine.
	 */
	if (s->count >= CONFIG_MAX_CPUS) {
		printf("SMP: more than %d cores, ignoring the rest\n",
		    CONFIG_MAX_CPUS);
		return 1;
	}

	cpu_mpidr[s->count++] = fdt_read_cells(p, s->addr_cells) &
	    MPIDR_AFF_MASK;

	return 0;
}

/*===========================================================================*
 *				discover_cpus				     *
 *===========================================================================*/
/*
 * Fill cpu_mpidr[] and return how many cores there are, with this core first.
 *
 * Putting the boot core at index zero is not cosmetic. TPIDR_EL1 has to hold
 * this core's number from the very first kernel entry, long before anything
 * has read the device tree, and the only number available that early is zero.
 * Renumbering here is what makes that true rather than assumed.
 */
static unsigned
discover_cpus(void)
{
	struct cpu_scan scan;
	const void *dtb;
	u64_t me = this_mpidr();
	unsigned i;

	memset(&scan, 0, sizeof(scan));

	dtb = (const void *)phys2vir(boot_dtb);
	if (!fdt_valid(dtb))
		return 1;

	(void)fdt_walk(dtb, find_cpus, &scan);

	if (scan.count == 0)
		return 1;

	for (i = 0; i < scan.count; i++)
		if (cpu_mpidr[i] == me)
			break;

	if (i == scan.count) {
		/*
		 * This core is not in the list the tree gives. Rather than
		 * start cores while unsure which one is running, fall back to
		 * the one core that is definitely here.
		 */
		printf("SMP: this core (%08lx) is not in the device tree\n",
		    (unsigned long)me);
		cpu_mpidr[0] = me;
		return 1;
	}

	if (i != 0) {
		cpu_mpidr[i] = cpu_mpidr[0];
		cpu_mpidr[0] = me;
	}

	return scan.count;
}

/*===========================================================================*
 *				smp_start_aps				     *
 *===========================================================================*/
/*
 * Start the secondaries, one at a time.
 *
 * One at a time and not in a batch, because until a core reaches
 * pg_ap_enter_high() it is standing on the single early stack in head.S.
 * Waiting for it to report is what makes that stack safe, and it also means a
 * core that never comes up costs one wait rather than confusing the next.
 */
static void
smp_start_aps(void)
{
	phys_bytes entry = vir2phys(_start_ap);
	unsigned cpu;
	long r;
	int spins;

	for (cpu = 0; cpu < ncpus; cpu++) {
		if (cpu == bsp_cpu_id)
			continue;

		ap_cpu_ready = -1;
		barrier();

		r = psci_cpu_on(cpu_mpidr[cpu], entry, cpu);
		if (r != PSCI_SUCCESS) {
			printf("SMP: PSCI refused to start CPU %d: %d\n",
			    cpu, (int)r);
			continue;
		}

		for (spins = AP_BOOT_SPINS; spins > 0; spins--) {
			if (ap_cpu_ready == (int)cpu)
				break;
			arch_pause();
		}

		if (ap_cpu_ready != (int)cpu) {
			printf("SMP: CPU %d did not come up\n", cpu);
			continue;
		}

		cpu_set_flag(cpu, CPU_IS_READY);
	}

	bsp_finish_booting();
	NOT_REACHABLE;
}

/*===========================================================================*
 *				smp_init				     *
 *===========================================================================*/
void
smp_init(void)
{
	ncpus = discover_cpus();
	bsp_cpu_id = 0;

	cpu_set_flag(0, CPU_IS_BSP);

	if (ncpus == 1) {
		/*
		 * Returning would have main.c call bsp_finish_booting() for
		 * us, which is exactly what is wanted here.
		 */
		return;
	}

	printf("SMP: %d cores\n", ncpus);

	smp_start_aps();
	NOT_REACHABLE;
}

/*===========================================================================*
 *				ap_finish_booting			     *
 *===========================================================================*/
/*
 * A secondary, running in the upper half on its own kernel stack, joining the
 * system.
 *
 * boot_lock as well as the big kernel lock, for the same reason i386 takes
 * both: what happens between here and ap_boot_finished() must not interleave
 * with another core doing the same, and the big lock alone is dropped and
 * retaken inside some of it.
 */
static void
ap_finish_booting(void)
{
	unsigned cpu = cpuid;

	/*
	 * Report before taking anything.
	 *
	 * The boot core is spinning on this, and it is holding the big kernel
	 * lock while it spins - it has held it since main() and does not let
	 * go until it returns to user. Reporting after BKL_LOCK() would mean
	 * waiting for a lock whose holder is waiting for this, which is a
	 * machine that boots with one core and prints the others' greetings
	 * afterwards.
	 *
	 * Safe this early because the shared stack in head.S was left behind
	 * two calls ago: this core has been on its own per-CPU stack since
	 * pg_ap_enter_high().
	 */
	ap_cpu_ready = (int)cpu;
	barrier();

	spinlock_lock(&boot_lock);
	BKL_LOCK();

	/*
	 * Vectors, the timer registers EL0 is allowed to read, and the kernel
	 * stack pointer: all per-CPU, and none of them set by the core that
	 * started this one.
	 */
	arch_init();

	/* This core's half of the interrupt controller. */
	gic_cpu_init();

	/*
	 * And its own permission to translate user addresses. TCR.EPD0 was
	 * set by pg_ap_drop_identity() on the way up and nothing else would
	 * clear it here - the boot core's pg_load() only spoke for the boot
	 * core. Without this the first message copied from a process on this
	 * core faults, and the kernel reports the address as a bad user
	 * pointer, which is the one thing it is not.
	 */
	pg_enable_user_walks();

	fpu_init();

	cycles_accounting_init();

	if (app_cpu_init_timer(system_hz))
		panic("CPU %d has no clock source", cpu);

	get_cpulocal_var(proc_ptr) = get_cpulocal_var_ptr(idle_proc);
	get_cpulocal_var(bill_ptr) = get_cpulocal_var_ptr(idle_proc);

	printf("CPU %d is up\n", cpu);

	ap_boot_finished(cpu);
	spinlock_unlock(&boot_lock);

	switch_to_user();
	NOT_REACHABLE;
}

/*===========================================================================*
 *				smp_ap_boot				     *
 *===========================================================================*/
/*
 * Where a secondary arrives in the upper half, on its own stack.
 *
 * TPIDR_EL1 is written here and never again: from this instruction on, cpuid
 * works on this core, which is what every per-CPU variable in the kernel
 * depends on.
 */
void
smp_ap_boot(unsigned cpu)
{
	__asm__ volatile("msr tpidr_el1, %0" :: "r"((u64_t)cpu));

	/* The low half was needed for one instruction and is a hazard now. */
	pg_ap_drop_identity();

	ap_finish_booting();
	NOT_REACHABLE;
}

/*===========================================================================*
 *				smp_ap_start				     *
 *===========================================================================*/
/*
 * And where it arrives from head.S: at EL1, MMU off, on the shared early
 * stack, running at a physical program counter.
 *
 * Everything reachable from here has to be position-independent, which
 * -mcmodel=small makes it, and must not touch anything the kernel has moved
 * to an upper-half address - the console among them. There is nothing to
 * print with until the branch below has happened.
 */
void
smp_ap_start(unsigned cpu)
{
	pg_ap_paging_on();
	pg_ap_enter_high(cpu, smp_ap_boot);
	NOT_REACHABLE;
}

/*===========================================================================*
 *			     arch_send_smp_schedule_ipi			     *
 *===========================================================================*/
void
arch_send_smp_schedule_ipi(unsigned cpu)
{
	gic_send_ipi(cpu, GIC_IPI_SCHED);
}

/*===========================================================================*
 *				arch_smp_halt_cpu			     *
 *===========================================================================*/
void
arch_smp_halt_cpu(void)
{
	/*
	 * Let go of the kernel before stopping, or the cores still running
	 * would wait on this one forever.
	 */
	BKL_UNLOCK();

	for (;;)
		__asm__ volatile("wfi");
}

/*===========================================================================*
 *				smp_shutdown_aps			     *
 *===========================================================================*/
void
smp_shutdown_aps(void)
{
	unsigned cpu;

	if (ncpus == 1)
		goto done;

	/* The other cores have to be able to enter the kernel to be told. */
	BKL_UNLOCK();

	for (cpu = 0; cpu < ncpus; cpu++) {
		if (cpu == cpuid || !cpu_test_flag(cpu, CPU_IS_READY))
			continue;
		gic_send_ipi(cpu, GIC_IPI_HALT);
	}

	/*
	 * Not waiting for them to acknowledge. i386 waits, and can, because
	 * its halt path writes back before stopping; here the caller is on
	 * its way to a reset that takes the whole machine with it, and a core
	 * that has already wedged would turn a shutdown into a hang.
	 */
	BKL_LOCK();

done:
	ncpus = 1;
}
