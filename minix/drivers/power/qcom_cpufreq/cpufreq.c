/*
 * The qcom_cpufreq service (qcom_cpufreq.h; port/clover/hw-cpu.md, path A
 * of section 9 and section 13).  Started, it runs one stage and leaves: a
 * cluster's rate is a setting of the hardware, there is nothing to serve
 * afterwards.  A run that is done reports ENXIO to RS, as a driver that
 * found nothing does, and the system goes on.
 *
 *	minix-service up /service/qcom_cpufreq
 *	minix-service up /service/qcom_cpufreq -args "stage=set gold=1747"
 *
 *   stage=probe   (the default) everything read and printed - the fuses,
 *                 this chip's corners, aux, the rails, each cluster's mux
 *                 and PLL, how high each rail would let it go; nothing
 *                 written
 *   stage=set     the clusters given below to their targets
 *   gold=, silver=   the target: a point of the cluster's table for this
 *                 chip ("1113", "1401.6", "1747", "1536"...), "max" - the
 *                 highest the rail holds - or "aux", GPLL0 (the undo).  A
 *                 cluster not named is not touched
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

#include <stdlib.h>

struct log cf_log = {
	.name = "qcom_cpufreq",
	.log_level = LEVEL_INFO,
	.log_func = default_log
};

static struct cf cf;
static int stage = CF_PROBE;
static unsigned steps = 1, hold;
static char gold[16], silver[16];
static const char *target[CF_NCLUSTER];

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
		else if (strcmp(value, "probe") != 0) {
			log_warn(&cf_log, "stage=%s is neither probe nor set\n",
			    value);
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

static int
cf_init(int UNUSED(type), sef_init_info_t *UNUSED(info))
{
	void *dtb;
	int r;

	if (parse() != OK)
		return EINVAL;
	if ((dtb = fdt_fetch()) == NULL) {
		log_info(&cf_log, "no device tree; not starting\n");
		return ENXIO;
	}
	r = cf_find(dtb, &cf);
	free(dtb);
	if (r != OK) {
		log_info(&cf_log, "not this machine's blocks (%d); not "
		    "starting\n", r);
		return ENXIO;
	}
	cf.steps = steps;
	cf.hold = hold;
	r = cf_run(&cf, stage, target);
	log_info(&cf_log, "stage %s: %s (%d)\n", stage == CF_SET ? "set" :
	    "probe", r == OK ? "done" : "failed", r);
	/* The stage is the whole job: ENXIO is what RS expects of a leaver. */
	return r == OK ? ENXIO : r;
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

	sef_setcb_init_fresh(cf_init);
	sef_startup();
	return 0;
}
