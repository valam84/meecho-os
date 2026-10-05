/* This file contains the scheduling policy for SCHED
 *
 * The entry points are:
 *   do_noquantum:        Called on behalf of process' that run out of quantum
 *   do_start_scheduling  Request to start scheduling a proc
 *   do_stop_scheduling   Request to stop scheduling a proc
 *   do_nice		  Request to change the nice level on a proc
 *   init_scheduling      Called from main.c to set up/prepare scheduling
 */
#include "sched.h"
#include "schedproc.h"
#include <assert.h>
#include <minix/com.h>
#include <machine/archtypes.h>

static unsigned balance_timeout;

#define BALANCE_TIMEOUT	5 /* how often to balance queues in seconds */

static int schedule_process(struct schedproc * rmp, unsigned flags);

#define SCHEDULE_CHANGE_PRIO	0x1
#define SCHEDULE_CHANGE_QUANTUM	0x2
#define SCHEDULE_CHANGE_CPU	0x4

#define SCHEDULE_CHANGE_ALL	(	\
		SCHEDULE_CHANGE_PRIO	|	\
		SCHEDULE_CHANGE_QUANTUM	|	\
		SCHEDULE_CHANGE_CPU		\
		)

#define schedule_process_local(p)	\
	schedule_process(p, SCHEDULE_CHANGE_PRIO | SCHEDULE_CHANGE_QUANTUM)
#define schedule_process_migrate(p)	\
	schedule_process(p, SCHEDULE_CHANGE_CPU)

#define DEFAULT_USER_TIME_SLICE 200

/* processes created by RS are sysytem processes */
#define is_system_proc(p)	((p)->parent == RS_PROC_NR)

#ifdef CONFIG_SMP
/*
 * Where user processes go, and when they move.
 *
 * System processes live on the boot CPU and are never moved. A user process
 * is placed when it starts, and may be moved when it runs out of quantum -
 * the one moment the kernel says it is computing rather than waiting, and
 * the moment it is off every run queue, so moving it costs nothing but its
 * cache.
 *
 * The load of a CPU is the number of user processes placed there that ran
 * out of quantum within BUSY_WINDOW_SECONDS: the ones competing for it.
 * Counting every process placed there, which is what this used to do,
 * weighs a sleeping shell the same as a compute job - and the count was
 * bumped again on every quantum and never taken back, so it only grew. A
 * process moves when its CPU has at least two more busy processes than the
 * least loaded one: a strict improvement, so it cannot oscillate; with more
 * jobs than CPUs the remainder stays where it is.
 *
 * The boot CPU takes user processes too, last among equals. Its system
 * processes sit in higher priority queues and preempt a user job the moment
 * they have work; what they leave idle is the boot CPU's time, which a
 * placement that never used it threw away - a quarter of the machine on
 * four cores (2026-10-04 on the CB2: three jobs ran as fast as one, four
 * took twice as long).
 *
 * The window is wide enough that a CPU shared by several busy processes,
 * each of which runs out of quantum only once per round, keeps counting all
 * of them.
 */
#define BUSY_WINDOW_SECONDS	2

static int cpu_procs[CONFIG_MAX_CPUS];	/* user processes placed per CPU */
static int cpu_dead[CONFIG_MAX_CPUS];	/* refused by the kernel: EBADCPU */
static clock_t busy_window;

static int is_busy(const struct schedproc *rmp, clock_t now)
{
	return (rmp->flags & RAN_OUT) &&
	    (clock_t)(now - rmp->last_noquantum) < busy_window;
}

/* Busy user processes per CPU. */
static void cpu_loads(int load[CONFIG_MAX_CPUS])
{
	struct schedproc *rmp;
	clock_t now = getticks();
	unsigned c;

	for (c = 0; c < CONFIG_MAX_CPUS; c++)
		load[c] = 0;

	for (rmp = schedproc; rmp < &schedproc[NR_PROCS]; rmp++) {
		if ((rmp->flags & (IN_USE | PLACED)) != (IN_USE | PLACED))
			continue;
		if (is_busy(rmp, now))
			load[rmp->cpu]++;
	}
}

/*
 * The best CPU other than "except" (-1 for none): least busy, then not the
 * boot CPU, then fewest processes placed, then lowest number. -1 if every
 * candidate is dead.
 */
static int least_loaded_cpu(const int load[CONFIG_MAX_CPUS], int except)
{
	unsigned c;
	int best = -1;

	for (c = 0; c < machine.processors_count; c++) {
		if (cpu_dead[c] || (int)c == except)
			continue;
		if (best >= 0) {
			int cb = (c == machine.bsp_id);
			int bb = ((unsigned)best == machine.bsp_id);

			if (load[c] > load[best])
				continue;
			if (load[c] == load[best]) {
				if (cb > bb)
					continue;
				if (cb == bb && cpu_procs[c] >= cpu_procs[best])
					continue;
			}
		}
		best = c;
	}
	return best;
}

/* Move the process's place in the accounting to "cpu". */
static void place_cpu(struct schedproc *rmp, unsigned cpu)
{
	if (rmp->flags & PLACED)
		cpu_procs[rmp->cpu]--;
	rmp->cpu = cpu;
	cpu_procs[cpu]++;
	rmp->flags |= PLACED;
}

static void unplace_cpu(struct schedproc *rmp)
{
	if (rmp->flags & PLACED)
		cpu_procs[rmp->cpu]--;
	rmp->flags &= ~PLACED;
}
#endif

/*
 * Choose a CPU for a process that is starting. Only here and in
 * rebalance(): otherwise a process stays where it was put, because that is
 * where the kernel has it.
 */
static void pick_cpu(struct schedproc * proc)
{
#ifdef CONFIG_SMP
	int load[CONFIG_MAX_CPUS];
	int cpu;

	if (machine.processors_count == 1 || is_system_proc(proc)) {
		unplace_cpu(proc);
		proc->cpu = machine.bsp_id;
		return;
	}

	cpu_loads(load);
	cpu = least_loaded_cpu(load, -1);
	if (cpu < 0)
		cpu = machine.bsp_id;
	place_cpu(proc, cpu);
#else
	proc->cpu = 0;
#endif
}

#ifdef CONFIG_SMP
/*
 * A user process just ran out of quantum on its CPU. Move it if another CPU
 * has at least two fewer busy processes, and reschedule it either way.
 */
static int rebalance(struct schedproc *rmp)
{
	int load[CONFIG_MAX_CPUS];
	unsigned from = rmp->cpu;
	int to, rv;

	if (machine.processors_count == 1 || is_system_proc(rmp) ||
	    !(rmp->flags & PLACED))
		return schedule_process_local(rmp);

	cpu_loads(load);
	to = least_loaded_cpu(load, from);
	if (to < 0 || load[from] < load[to] + 2)
		return schedule_process_local(rmp);

	place_cpu(rmp, to);
	if ((rv = schedule_process(rmp, SCHEDULE_CHANGE_ALL)) == OK)
		return OK;

	/* Where it was, then; and never there again if the CPU is gone. */
	if (rv == EBADCPU)
		cpu_dead[to] = 1;
	place_cpu(rmp, from);
	return schedule_process_local(rmp);
}
#endif

/*===========================================================================*
 *				do_noquantum				     *
 *===========================================================================*/

int do_noquantum(message *m_ptr)
{
	register struct schedproc *rmp;
	int rv, proc_nr_n;

	if (sched_isokendpt(m_ptr->m_source, &proc_nr_n) != OK) {
		printf("SCHED: WARNING: got an invalid endpoint in OOQ msg %u.\n",
		m_ptr->m_source);
		return EBADEPT;
	}

	rmp = &schedproc[proc_nr_n];
	if (rmp->priority < MIN_USER_Q) {
		rmp->priority += 1; /* lower priority */
	}

#ifdef CONFIG_SMP
	rmp->last_noquantum = getticks();
	rmp->flags |= RAN_OUT;
	rv = rebalance(rmp);
#else
	rv = schedule_process_local(rmp);
#endif
	if (rv != OK) {
		return rv;
	}
	return OK;
}

/*===========================================================================*
 *				do_stop_scheduling			     *
 *===========================================================================*/
int do_stop_scheduling(message *m_ptr)
{
	register struct schedproc *rmp;
	int proc_nr_n;

	/* check who can send you requests */
	if (!accept_message(m_ptr))
		return EPERM;

	if (sched_isokendpt(m_ptr->m_lsys_sched_scheduling_stop.endpoint,
		    &proc_nr_n) != OK) {
		printf("SCHED: WARNING: got an invalid endpoint in OOQ msg "
		"%d\n", m_ptr->m_lsys_sched_scheduling_stop.endpoint);
		return EBADEPT;
	}

	rmp = &schedproc[proc_nr_n];
#ifdef CONFIG_SMP
	unplace_cpu(rmp);
#endif
	rmp->flags = 0; /*&= ~IN_USE;*/

	return OK;
}

/*===========================================================================*
 *				do_start_scheduling			     *
 *===========================================================================*/
int do_start_scheduling(message *m_ptr)
{
	register struct schedproc *rmp;
	int rv, proc_nr_n, parent_nr_n;
	
	/* we can handle two kinds of messages here */
	assert(m_ptr->m_type == SCHEDULING_START || 
		m_ptr->m_type == SCHEDULING_INHERIT);

	/* check who can send you requests */
	if (!accept_message(m_ptr))
		return EPERM;

	/* Resolve endpoint to proc slot. */
	if ((rv = sched_isemtyendpt(m_ptr->m_lsys_sched_scheduling_start.endpoint,
			&proc_nr_n)) != OK) {
		return rv;
	}
	rmp = &schedproc[proc_nr_n];

	/* Populate process slot */
	rmp->endpoint     = m_ptr->m_lsys_sched_scheduling_start.endpoint;
	rmp->parent       = m_ptr->m_lsys_sched_scheduling_start.parent;
	rmp->max_priority = m_ptr->m_lsys_sched_scheduling_start.maxprio;
	if (rmp->max_priority >= NR_SCHED_QUEUES) {
		return EINVAL;
	}

	/* Inherit current priority and time slice from parent. Since there
	 * is currently only one scheduler scheduling the whole system, this
	 * value is local and we assert that the parent endpoint is valid */
	if (rmp->endpoint == rmp->parent) {
		/* We have a special case here for init, which is the first
		   process scheduled, and the parent of itself. */
		rmp->priority   = USER_Q;
		rmp->time_slice = DEFAULT_USER_TIME_SLICE;

		/*
		 * Since kernel never changes the cpu of a process, all are
		 * started on the BSP and the userspace scheduling hasn't
		 * changed that yet either, we can be sure that BSP is the
		 * processor where the processes run now.
		 */
#ifdef CONFIG_SMP
		rmp->cpu = machine.bsp_id;
		/* FIXME set the cpu mask */
#endif
	}
	
	switch (m_ptr->m_type) {

	case SCHEDULING_START:
		/* We have a special case here for system processes, for which
		 * quanum and priority are set explicitly rather than inherited 
		 * from the parent */
		rmp->priority   = rmp->max_priority;
		rmp->time_slice = m_ptr->m_lsys_sched_scheduling_start.quantum;
		break;
		
	case SCHEDULING_INHERIT:
		/* Inherit current priority and time slice from parent. Since there
		 * is currently only one scheduler scheduling the whole system, this
		 * value is local and we assert that the parent endpoint is valid */
		if ((rv = sched_isokendpt(m_ptr->m_lsys_sched_scheduling_start.parent,
				&parent_nr_n)) != OK)
			return rv;

		rmp->priority = schedproc[parent_nr_n].priority;
		rmp->time_slice = schedproc[parent_nr_n].time_slice;
		break;
		
	default: 
		/* not reachable */
		assert(0);
	}

	/* Take over scheduling the process. The kernel reply message populates
	 * the processes current priority and its time slice */
	if ((rv = sys_schedctl(0, rmp->endpoint, 0, 0, 0)) != OK) {
		printf("Sched: Error taking over scheduling for %d, kernel said %d\n",
			rmp->endpoint, rv);
		return rv;
	}
	rmp->flags = IN_USE;

	/* Schedule the process, giving it some quantum */
	pick_cpu(rmp);
	while ((rv = schedule_process(rmp, SCHEDULE_CHANGE_ALL)) == EBADCPU) {
		/* don't try this CPU ever again */
#ifdef CONFIG_SMP
		cpu_dead[rmp->cpu] = 1;
#endif
		pick_cpu(rmp);
	}

	if (rv != OK) {
		printf("Sched: Error while scheduling process, kernel replied %d\n",
			rv);
		return rv;
	}

	/* Mark ourselves as the new scheduler.
	 * By default, processes are scheduled by the parents scheduler. In case
	 * this scheduler would want to delegate scheduling to another
	 * scheduler, it could do so and then write the endpoint of that
	 * scheduler into the "scheduler" field.
	 */

	m_ptr->m_sched_lsys_scheduling_start.scheduler = SCHED_PROC_NR;

	return OK;
}

/*===========================================================================*
 *				do_nice					     *
 *===========================================================================*/
int do_nice(message *m_ptr)
{
	struct schedproc *rmp;
	int rv;
	int proc_nr_n;
	unsigned new_q, old_q, old_max_q;

	/* check who can send you requests */
	if (!accept_message(m_ptr))
		return EPERM;

	if (sched_isokendpt(m_ptr->m_pm_sched_scheduling_set_nice.endpoint, &proc_nr_n) != OK) {
		printf("SCHED: WARNING: got an invalid endpoint in OoQ msg "
		"%d\n", m_ptr->m_pm_sched_scheduling_set_nice.endpoint);
		return EBADEPT;
	}

	rmp = &schedproc[proc_nr_n];
	new_q = m_ptr->m_pm_sched_scheduling_set_nice.maxprio;
	if (new_q >= NR_SCHED_QUEUES) {
		return EINVAL;
	}

	/* Store old values, in case we need to roll back the changes */
	old_q     = rmp->priority;
	old_max_q = rmp->max_priority;

	/* Update the proc entry and reschedule the process */
	rmp->max_priority = rmp->priority = new_q;

	if ((rv = schedule_process_local(rmp)) != OK) {
		/* Something went wrong when rescheduling the process, roll
		 * back the changes to proc struct */
		rmp->priority     = old_q;
		rmp->max_priority = old_max_q;
	}

	return rv;
}

/*===========================================================================*
 *				schedule_process			     *
 *===========================================================================*/
static int schedule_process(struct schedproc * rmp, unsigned flags)
{
	int err;
	int new_prio, new_quantum, new_cpu, niced;

	/*
	 * No pick_cpu() here. This runs on every quantum and every nice, and
	 * re-picking here changed rmp->cpu without telling the kernel (only
	 * SCHEDULE_CHANGE_CPU does) and counted the process once more each
	 * time.
	 */
	if (flags & SCHEDULE_CHANGE_PRIO)
		new_prio = rmp->priority;
	else
		new_prio = -1;

	if (flags & SCHEDULE_CHANGE_QUANTUM)
		new_quantum = rmp->time_slice;
	else
		new_quantum = -1;

	if (flags & SCHEDULE_CHANGE_CPU)
		new_cpu = rmp->cpu;
	else
		new_cpu = -1;

	niced = (rmp->max_priority > USER_Q);

	if ((err = sys_schedule(rmp->endpoint, new_prio,
		new_quantum, new_cpu, niced)) != OK) {
		printf("PM: An error occurred when trying to schedule %d: %d\n",
		rmp->endpoint, err);
	}

	return err;
}


/*===========================================================================*
 *				init_scheduling				     *
 *===========================================================================*/
void init_scheduling(void)
{
	int r;

	balance_timeout = BALANCE_TIMEOUT * sys_hz();
#ifdef CONFIG_SMP
	busy_window = BUSY_WINDOW_SECONDS * sys_hz();
#endif

	if ((r = sys_setalarm(balance_timeout, 0)) != OK)
		panic("sys_setalarm failed: %d", r);
}

/*===========================================================================*
 *				balance_queues				     *
 *===========================================================================*/

/* This function in called every N ticks to rebalance the queues. The current
 * scheduler bumps processes down one priority when ever they run out of
 * quantum. This function will find all proccesses that have been bumped down,
 * and pulls them back up. This default policy will soon be changed.
 */
void balance_queues(void)
{
	struct schedproc *rmp;
	int r, proc_nr;

	for (proc_nr=0, rmp=schedproc; proc_nr < NR_PROCS; proc_nr++, rmp++) {
		if (rmp->flags & IN_USE) {
			if (rmp->priority > rmp->max_priority) {
				rmp->priority -= 1; /* increase priority */
				schedule_process_local(rmp);
			}
		}
	}

	if ((r = sys_setalarm(balance_timeout, 0)) != OK)
		panic("sys_setalarm failed: %d", r);
}
