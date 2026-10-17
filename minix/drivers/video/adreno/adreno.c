/*
 * The Adreno GPU service: so far its first step (adreno.h) - one run of the
 * stage it is given, the answers in the log, and no service left behind:
 * a probe that started nothing reports ENXIO to RS, as a driver that found
 * no hardware does, and the system goes on.
 *
 *	minix-service up /service/adreno -args "stage=probe tz=1"
 *
 *   stage=   probe (default), cx, gx, gpu, smmu, smr, cb0, fault - each
 *            one also does the ones before it; the last three write the
 *            SMMU and put it back (adreno_smmu.c)
 *   tz=1     ask TrustZone about the zap shader's calls (Q12)
 *   hold=N   stay N seconds before leaving - to see where it runs
 *            (ps -o cpuid): the stand cannot check "cpu 1", only a machine
 *   log=     log level, as other services
 *
 * The system.conf entry keeps it off the boot processor ("cpu 1"): if a
 * read here ever did stall the bus, the core stalled would be this one,
 * and the boot processor's tick - with the boot watchdog in it - would go
 * on (port/clover/hw-gpu.md, §12).
 */

#include "adreno.h"

#include <minix/sysutil.h>

#include <stdlib.h>
#include <string.h>

struct log ad_log = {
	.name = "adreno",
	.log_level = LEVEL_INFO,
	.log_func = default_log
};

static int stage = AD_PROBE;
static int tz;
static long hold;

static int
parse_stage(void)
{
	static const char *const names[] = { "probe", "cx", "gx", "gpu",
	    "smmu", "smr", "cb0", "fault" };
	char value[16];
	unsigned i;

	if (env_get_param("stage", value, sizeof(value)) != OK)
		return OK;
	for (i = 0; i < sizeof(names) / sizeof(names[0]); i++)
		if (strcmp(value, names[i]) == 0) {
			stage = (int)i;
			return OK;
		}
	log_warn(&ad_log, "stage=%s is none of probe, cx, gx, gpu, smmu, smr, "
	    "cb0, fault\n", value);
	return EINVAL;
}

static int
adreno_init(int UNUSED(type), sef_init_info_t *UNUSED(info))
{
	void *dtb;
	int r;

	if (parse_stage() != OK)
		return EINVAL;
	if ((dtb = fdt_fetch()) == NULL) {
		log_info(&ad_log, "no device tree; not starting\n");
		return ENXIO;
	}
	r = adreno_run(dtb, stage, tz);
	free(dtb);
	if (r == OK)
		log_info(&ad_log, "done; not starting (no render node yet)\n");
	if (hold > 0)
		tickdelay((clock_t)(hold * sys_hz()));
	/*
	 * Nothing to serve yet: the run is the whole job.  ENXIO is what RS
	 * expects of a driver that leaves, and minix-service says so.
	 */
	return r == OK ? ENXIO : r;
}

int
main(int argc, char *argv[])
{
	long v;

	env_setargs(argc, argv);
	if (env_parse("log", "d", 0, &v, LEVEL_NONE, LEVEL_TRACE) == EP_SET)
		ad_log.log_level = (int)v;
	if (env_parse("tz", "d", 0, &v, 0, 1) == EP_SET)
		tz = (int)v;
	if (env_parse("hold", "d", 0, &v, 0, 60) == EP_SET)
		hold = v;

	sef_setcb_init_fresh(adreno_init);
	sef_startup();

	/* Not reached: init never answers OK. */
	return 0;
}
