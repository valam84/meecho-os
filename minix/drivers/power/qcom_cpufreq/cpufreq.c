/*
 * The qcom_cpufreq service (qcom_cpufreq.h; port/clover/hw-cpu.md, path A
 * of section 9 and section 13).  Started with stage=probe or stage=set, it
 * runs that stage and leaves: a cluster's rate is a setting of the
 * hardware, there is nothing to serve afterwards.  Such a run reports ENXIO
 * to RS, as a driver that found nothing does, and the system goes on.  With
 * stage=gov it stays, and every period= ms moves each cluster by its load
 * (cf_gov.c).
 *
 *	minix-service up /service/qcom_cpufreq
 *	minix-service up /service/qcom_cpufreq -args "stage=set gold=1747"
 *	minix-service up /service/qcom_cpufreq -args "stage=gov gov=ondemand"
 *
 *   stage=probe   (the default) everything read and printed - the fuses,
 *                 this chip's corners, aux, the rails, each cluster's mux
 *                 and PLL, how high each rail would let it go; nothing
 *                 written
 *   stage=set     the clusters given below to their targets
 *   stage=gov     the governor: the clusters follow the load, the service
 *                 stays
 *   gold=, silver=   the target: a point of the cluster's table for this
 *                 chip ("1113", "1401.6", "1747", "1536"...), "max" - the
 *                 highest the rail holds - or "aux", GPLL0 (the undo).  A
 *                 cluster not named is not touched.  With stage=gov, the
 *                 highest point the governor may take ("max": no cap); a
 *                 cluster not named is governed without one
 *   gov=          performance, powersave or ondemand (the default)
 *   period=MS     how often the governor looks (100; 20..1000)
 *   up=, aim=, downs=   ondemand's: a load at or above up% (80) goes to
 *                 the top; below, the lowest point where it would be aim%
 *                 (60) or less - down only after downs (3) periods asked
 *   bootmpidr=    the boot core's MPIDR (0): the kernel numbers the cores
 *                 from it (cf_gov_cpus())
 *   report=S      a line per cluster every S seconds: switches and the
 *                 share of time at each point (0: only when the service
 *                 goes)
 *   steps=0       straight to the target; by default every point of the
 *                 table on the way up is a stop of its own
 *   hold=N        stay N seconds at each stop - to time the cores there
 *   log=          log level, as other services
 *
 * The system.conf entry keeps it off the boot processor ("cpu 1"), as the
 * adreno service and the probe: if a write here ever stalled the bus, the
 * core stalled would be this one, and the boot processor's tick - with the
 * boot watchdog in it - would go on.
 */

#include "qcom_cpufreq.h"

#include <minix/fdt.h>
#include <minix/sysutil.h>

#include <signal.h>
#include <stdlib.h>

struct log cf_log = {
	.name = "qcom_cpufreq",
	.log_level = LEVEL_INFO,
	.log_func = default_log
};

#define CPU_STATE_IDLE	4		/* CP_IDLE: the kernel's account */
#define CHECK_EVERY_MS	1000		/* the rails looked at, at rest */

static struct cf cf;
static int stage = CF_PROBE;
static unsigned steps = 1, hold;
static char gold[16], silver[16];
static const char *target[CF_NCLUSTER];

/* stage=gov */
static unsigned period_ms = 100, report_s;
static uint64_t boot_mpidr;
static struct cf_govpol pol;		/* until cf_find() has cleared cf */
static int cluster_of[CF_MAX_CPUS];
static unsigned ncpus;
static uint64_t last_busy[CF_MAX_CPUS];
static clock_t last_t, last_check, last_report;
static int governing;

void
cf_delay_us(unsigned us)
{
	(void)micro_delay(us);
}

void
cf_pause(unsigned seconds)
{
	(void)tickdelay((clock_t)seconds * sys_hz());
}

static int
parse(void)
{
	char value[16];

	if (env_get_param("stage", value, sizeof(value)) == OK) {
		if (strcmp(value, "set") == 0)
			stage = CF_SET;
		else if (strcmp(value, "gov") == 0)
			stage = CF_GOV;
		else if (strcmp(value, "probe") != 0) {
			log_warn(&cf_log, "stage=%s is none of probe, set, gov\n",
			    value);
			return EINVAL;
		}
	}
	pol.policy = CF_GOV_ONDEMAND;
	if (env_get_param("gov", value, sizeof(value)) == OK) {
		if (strcmp(value, "performance") == 0)
			pol.policy = CF_GOV_PERFORMANCE;
		else if (strcmp(value, "powersave") == 0)
			pol.policy = CF_GOV_POWERSAVE;
		else if (strcmp(value, "ondemand") != 0) {
			log_warn(&cf_log, "gov=%s is none of performance, "
			    "powersave, ondemand\n", value);
			return EINVAL;
		}
	}
	if (env_get_param("gold", gold, sizeof(gold)) == OK)
		target[CF_GOLD] = gold;
	if (env_get_param("silver", silver, sizeof(silver)) == OK)
		target[CF_SILVER] = silver;
	if (stage == CF_PROBE && (target[CF_GOLD] || target[CF_SILVER])) {
		log_warn(&cf_log, "gold= and silver= need stage=set\n");
		return EINVAL;
	}
	return OK;
}

/*===========================================================================*
 *    the governor's periods                                                 *
 *===========================================================================*/
static void
arm(void)
{
	/* Only ondemand has a load to follow; the others look at the rails. */
	clock_t t = (clock_t)(pol.policy == CF_GOV_ONDEMAND ? period_ms :
	    CHECK_EVERY_MS) * sys_hz() / 1000;

	if (sys_setalarm(t > 0 ? t : 1, 0) != OK)
		log_warn(&cf_log, "no alarm: the governor stops here\n");
}

/*
 * One period: each core's time not idle since the last, from the kernel's
 * per-core account (GET_CPUTICKS; the same mib gives kern.cp_time), as a
 * share of the time that passed; a cluster's load is its busiest core's.
 * An idle core is charged its idle time only when it wakes, which does not
 * matter here: what counts is the time it was busy.
 */
static void
period(clock_t now)
{
	uint64_t ticks[MINIX_CPUSTATES], busy;
	unsigned load[CF_NCLUSTER] = { 0, 0 }, cpu, l, i;
	clock_t dt = now - last_t;
	int id;

	if (dt <= 0)
		dt = 1;
	for (cpu = 0; cpu < ncpus; cpu++) {
		if (sys_getcputicks(ticks, cpu) != OK)
			continue;
		for (busy = 0, i = 0; i < MINIX_CPUSTATES; i++)
			if (i != CPU_STATE_IDLE)
				busy += ticks[i];
		l = (unsigned)((busy - last_busy[cpu]) * 100 / (uint64_t)dt);
		last_busy[cpu] = busy;
		if (l > 100)
			l = 100;
		if (l >= pol.up)
			log_debug(&cf_log, "core %u: %u%% in %d ticks\n", cpu, l,
			    (int)dt);
		if ((id = cluster_of[cpu]) != CF_NO_CLUSTER && l > load[id])
			load[id] = l;
	}
	last_t = now;
	for (id = 0; id < CF_NCLUSTER; id++)
		(void)cf_gov_step(&cf, &cf.cl[id], &cf.gov[id], &cf.pol,
		    load[id], (unsigned)dt);
	if ((now - last_check) * 1000 >= (clock_t)CHECK_EVERY_MS * sys_hz()) {
		for (id = 0; id < CF_NCLUSTER; id++)
			(void)cf_gov_check(&cf, &cf.cl[id], &cf.gov[id]);
		last_check = now;
	}
	if (report_s > 0 && now - last_report >= (clock_t)report_s *
	    sys_hz()) {
		for (id = 0; id < CF_NCLUSTER; id++)
			cf_gov_report(&cf.cl[id], &cf.gov[id]);
		last_report = now;
	}
}

static int
gov_cpus(const void *dtb)
{
	uint64_t ticks[MINIX_CPUSTATES];
	unsigned cpu, i;
	int r;

	if ((r = cf_gov_cpus(dtb, boot_mpidr, cluster_of, &ncpus)) != OK) {
		log_warn(&cf_log, "the cores in the tree: none, or not the "
		    "boot core 0x%llx (%d)\n", (unsigned long long)boot_mpidr,
		    r);
		return r;
	}
	for (cpu = 0; cpu < ncpus; cpu++) {
		log_info(&cf_log, "core %u: %s\n", cpu,
		    cluster_of[cpu] == CF_GOLD ? "gold" :
		    cluster_of[cpu] == CF_SILVER ? "silver" : "neither");
		last_busy[cpu] = 0;
		if (sys_getcputicks(ticks, cpu) == OK)
			for (i = 0; i < MINIX_CPUSTATES; i++)
				if (i != CPU_STATE_IDLE)
					last_busy[cpu] += ticks[i];
	}
	last_t = last_check = last_report = getticks();
	return OK;
}

static int
cf_init(int UNUSED(type), sef_init_info_t *UNUSED(info))
{
	static const char *const sname[] = { "probe", "set", "gov" };
	void *dtb;
	int r;

	if (parse() != OK)
		return EINVAL;
	if ((dtb = fdt_fetch()) == NULL) {
		log_info(&cf_log, "no device tree; not starting\n");
		return ENXIO;
	}
	r = cf_find(dtb, &cf);
	cf.pol = pol;
	if (r == OK && stage == CF_GOV)
		r = gov_cpus(dtb);
	free(dtb);
	if (r != OK) {
		log_info(&cf_log, "not this machine's blocks (%d); not "
		    "starting\n", r);
		return ENXIO;
	}
	cf.steps = steps;
	cf.hold = hold;
	r = cf_run(&cf, stage, target);
	log_info(&cf_log, "stage %s: %s (%d)\n", sname[stage], r == OK ?
	    stage == CF_GOV ? "governing" : "done" : "failed", r);
	if (stage == CF_GOV && r == OK) {
		governing = 1;
		arm();
		return OK;
	}
	/* The stage is the whole job: ENXIO is what RS expects of a leaver. */
	return r == OK ? ENXIO : r;
}

/*
 * The governor goes: where the time went, and every cluster it governs to
 * its top - a system left at aux would crawl until the next boot.
 */
static void
cf_signal(int signo)
{
	static const struct cf_govpol top = { CF_GOV_PERFORMANCE, 0, 0, 0 };
	int id;

	if (signo != SIGTERM || !governing)
		return;
	for (id = 0; id < CF_NCLUSTER; id++) {
		cf_gov_report(&cf.cl[id], &cf.gov[id]);
		if (!cf.gov[id].frozen && cf.gov[id].n > 0)
			(void)cf_gov_step(&cf, &cf.cl[id], &cf.gov[id], &top,
			    100, 0);
	}
	exit(0);
}

int
main(int argc, char *argv[])
{
	long v;

	env_setargs(argc, argv);
	if (env_parse("log", "d", 0, &v, LEVEL_NONE, LEVEL_TRACE) == EP_SET)
		cf_log.log_level = (int)v;
	if (env_parse("steps", "d", 0, &v, 0, 1) == EP_SET)
		steps = (unsigned)v;
	if (env_parse("hold", "d", 0, &v, 0, 600) == EP_SET)
		hold = (unsigned)v;
	pol.up = 80;
	pol.aim = 60;
	pol.downs = 3;
	if (env_parse("period", "d", 0, &v, 20, 1000) == EP_SET)
		period_ms = (unsigned)v;
	if (env_parse("up", "d", 0, &v, 10, 100) == EP_SET)
		pol.up = (unsigned)v;
	if (env_parse("aim", "d", 0, &v, 10, 100) == EP_SET)
		pol.aim = (unsigned)v;
	if (env_parse("downs", "d", 0, &v, 1, 100) == EP_SET)
		pol.downs = (unsigned)v;
	if (env_parse("report", "d", 0, &v, 0, 86400) == EP_SET)
		report_s = (unsigned)v;
	if (env_parse("bootmpidr", "x", 0, &v, 0, 0xff00ffffffL) == EP_SET)
		boot_mpidr = (uint64_t)v;

	sef_setcb_init_fresh(cf_init);
	sef_setcb_signal_handler(cf_signal);
	sef_startup();

	/* stage=gov: the periods; nothing else comes. */
	for (;;) {
		message m;
		int ipc_status, r;

		if ((r = sef_receive_status(ANY, &m, &ipc_status)) != OK)
			panic("sef_receive failed: %d", r);
		if (is_ipc_notify(ipc_status) &&
		    _ENDPOINT_P(m.m_source) == CLOCK && governing) {
			period(m.m_notify.timestamp);
			arm();
		}
	}
	return 0;
}
