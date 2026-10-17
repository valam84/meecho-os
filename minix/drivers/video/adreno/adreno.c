/*
 * The Adreno GPU service.  Started as a driver is, it brings the GPU up
 * for good and serves its render node, /dev/dri/renderD128, as the msm
 * DRM interface Mesa's freedreno speaks (adreno_drm.h; hw-gpu.md, step
 * 7).  Given a stage, it runs that stage of the first steps once instead
 * (adreno.h) - the answers in the log, everything put back - and leaves:
 * a run that started nothing reports ENXIO to RS, as a driver that found
 * no hardware does, and the system goes on.
 *
 *	minix-service up /service/adreno
 *	minix-service up /service/adreno -args "stage=probe tz=1"
 *
 *   stage=   drm (what no stage= means): the render node.  probe, cx,
 *            gx, gpu, smmu, smr, cb0, fault, zap, cp - each one also does
 *            the ones before it; smr, cb0 and fault write the SMMU and put
 *            it back (adreno_smmu.c); zap has TrustZone load the zap
 *            shader, or resume it (adreno_zap.c); cp starts the command
 *            processor (adreno_cp.c)
 *   tz=1     ask TrustZone about the zap shader's calls (Q12)
 *   route=0  stages cb0 and fault without routing the GPU's stream
 *            (hw-gpu.md, §15, §16)
 *   fwdir=   where the firmware files are (adreno_fw.c)
 *   hang=MS  a job that makes no progress this long, twice, is a hang
 *            (2000; the GPU runs at the crystal's 19.2 MHz yet)
 *   memmb=, clientmb=   buffers in all and per client, MiB (768, 512)
 *   hold=N   stay N seconds before leaving a stage - to see where it runs
 *            (ps -o cpuid): the stand cannot check "cpu 1", only a machine
 *   log=     log level, as other services
 *
 * The system.conf entry keeps it off the boot processor ("cpu 1"): if a
 * read here ever did stall the bus, the core stalled would be this one,
 * and the boot processor's tick - with the boot watchdog in it - would go
 * on (port/clover/hw-gpu.md, §12).
 */

#include "adreno_drm.h"

#include <minix/sysutil.h>
#include <minix/timers.h>

#include <signal.h>
#include <stdlib.h>
#include <string.h>

struct log ad_log = {
	.name = "adreno",
	.log_level = LEVEL_INFO,
	.log_func = default_log
};

#define AD_DRM	(-1)			/* not a stage: the render node */

static int stage = AD_DRM;
static int tz;
static long hold;
static long mem_total_mb = 768, mem_client_mb = 512;
static int serving;
int ad_route = 1;

static int
parse_stage(void)
{
	static const char *const names[] = { "probe", "cx", "gx", "gpu",
	    "smmu", "smr", "cb0", "fault", "zap", "cp" };
	char value[16];
	unsigned i;

	if (env_get_param("fwdir", ad_fwdir, sizeof(ad_fwdir)) != OK)
		strlcpy(ad_fwdir, "/libdata/firmware/qcom", sizeof(ad_fwdir));
	if (env_get_param("stage", value, sizeof(value)) != OK ||
	    strcmp(value, "drm") == 0)
		return OK;
	for (i = 0; i < sizeof(names) / sizeof(names[0]); i++)
		if (strcmp(value, names[i]) == 0) {
			stage = (int)i;
			return OK;
		}
	log_warn(&ad_log, "stage=%s is none of drm, probe, cx, gx, gpu, smmu, "
	    "smr, cb0, fault, zap, cp\n", value);
	return EINVAL;
}

/*
 * The render node: the tree, the clock blocks as found (Q1-Q3), then the
 * GPU up for good (adreno_gpu.c) - and only then the node announced.
 */
static int
serve(const void *dtb)
{
	int r;

	if ((r = adreno_find(dtb, &ad.dt)) != OK)
		return r;
	adreno_find_report(&ad.dt);
	if ((r = adreno_map(dtb)) != OK || (r = cc_report()) != OK)
		return r;
	if ((r = gpu_serve()) != OK) {
		log_warn(&ad_log, "the GPU did not come up: %d; not serving\n",
		    r);
		return r;
	}
	adreno_drm_limits((size_t)mem_total_mb << 20,
	    (size_t)mem_client_mb << 20);
	adreno_drm_init();
	chardriver_announce();
	serving = 1;
	return OK;
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
	if (stage == AD_DRM) {
		r = serve(dtb);
		free(dtb);
		return r;
	}
	r = adreno_run(dtb, stage, tz);
	free(dtb);
	if (r == OK)
		log_info(&ad_log, "done; not starting (stage %d)\n", stage);
	if (hold > 0)
		tickdelay((clock_t)(hold * sys_hz()));
	/*
	 * A stage is the whole job.  ENXIO is what RS expects of a driver
	 * that leaves, and minix-service says so.
	 */
	return r == OK ? ENXIO : r;
}

/*
 * The service goes: nothing more into the ring, every client's wait
 * answered and every client's space freed, then the GPU down and
 * everything as found (Q23, Q17, Q11).
 */
static void
adreno_signal(int signo)
{
	if (signo != SIGTERM || !serving)
		return;
	log_info(&ad_log, "going down; the GPU off\n");
	gpu_stop();
	adreno_drm_shutdown();
	gpu_down();
	exit(0);
}

int
main(int argc, char *argv[])
{
	message m;
	int ipc_status, r;
	long v;

	env_setargs(argc, argv);
	if (env_parse("log", "d", 0, &v, LEVEL_NONE, LEVEL_TRACE) == EP_SET)
		ad_log.log_level = (int)v;
	if (env_parse("tz", "d", 0, &v, 0, 1) == EP_SET)
		tz = (int)v;
	if (env_parse("route", "d", 0, &v, 0, 1) == EP_SET)
		ad_route = (int)v;
	if (env_parse("hold", "d", 0, &v, 0, 60) == EP_SET)
		hold = v;
	if (env_parse("hang", "d", 0, &v, 100, 60000) == EP_SET)
		ad_hang_ms = v;
	if (env_parse("memmb", "d", 0, &v, 16, 4096) == EP_SET)
		mem_total_mb = v;
	if (env_parse("clientmb", "d", 0, &v, 16, 4096) == EP_SET)
		mem_client_mb = v;

	sef_setcb_init_fresh(adreno_init);
	sef_setcb_signal_handler(adreno_signal);
	sef_startup();

	/*
	 * Serving: the lines and the clock here; everything else is a
	 * request on the render node (adreno_drm.c, libdrmdriver).
	 */
	for (;;) {
		if ((r = sef_receive_status(ANY, &m, &ipc_status)) != OK)
			panic("sef_receive failed: %d", r);
		if (is_ipc_notify(ipc_status)) {
			if (_ENDPOINT_P(m.m_source) == HARDWARE)
				gpu_intr(m.m_notify.interrupts);
			/* Deadlines: waits (libdrmdriver), the hang check. */
			if (_ENDPOINT_P(m.m_source) == CLOCK)
				expire_timers(m.m_notify.timestamp);
			continue;
		}
		chardriver_process(&adreno_chardriver, &m, ipc_status);
	}
	return 0;
}
