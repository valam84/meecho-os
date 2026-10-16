/*
 * dwc3_ecm: the driver's life (dwc3_ecm.h).
 *
 * Found by the wrapper's compatible ("qcom,dwc3"); a machine without one
 * (QEMU's virt, the CB2) gets a line saying so and nothing else.  The
 * clocks, resets and power domain the tree names must all come from the
 * SoC's clock controller (GCC), which qcom_soc maps; a tree that points
 * them elsewhere is not trusted.
 *
 * At this milestone the service starts, reports and then waits for nothing
 * - the controller is not touched (port/clover/hw-net.md, step 2).
 *
 * Arguments: log=N (0 none .. 4 trace).
 */

#include "dwc3_ecm.h"
#include "qcom_soc.h"

#include <minix/sysutil.h>
#include <minix/vm.h>

#include <sys/mman.h>

#include <stdlib.h>

struct log dwc3_log = {
	.name = "dwc3",
	.log_level = LEVEL_INFO,
	.log_func = default_log
};

static struct dwc3_dt dt;

volatile uint8_t *
dwc3_map(u64_t pa, u64_t size, const char *what)
{
	void *v;

	if ((v = vm_map_phys(SELF, (void *)(phys_bytes)pa, (size_t)size)) ==
	    MAP_FAILED) {
		log_warn(&dwc3_log, "cannot map the %s at 0x%llx; not granted "
		    "by RS?\n", what, (unsigned long long)pa);
		return NULL;
	}
	return v;
}

int
dwc3_clk_state(const struct dwc3_clk *c, char *buf, size_t size)
{
	const char *name;
	uint32_t v = 0;
	int st;

	st = qsoc_clk_on(c->id, &v, &name);
	if (st == 1 || st == 0) {
		snprintf(buf, size, "%s %s", c->name, st ? "on" : "off");
		return st;
	}
	snprintf(buf, size, "%s ?(%u)", c->name, c->id);
	return -1;
}

static int
dwc3_init(int UNUSED(type), sef_init_info_t *UNUSED(info))
{
	void *dtb;
	long v;
	int r;

	if (env_parse("log", "d", 0, &v, LEVEL_NONE, LEVEL_TRACE) == EP_SET)
		dwc3_log.log_level = (int)v;
	if ((dtb = fdt_fetch()) == NULL) {
		log_info(&dwc3_log, "no device tree; nothing to do\n");
		return ENXIO;
	}
	if ((r = dwc3_find(dtb, &dt)) != OK) {
		if (r == ENXIO)
			log_info(&dwc3_log, "no USB wrapper this driver knows "
			    "(qcom,dwc3) in the tree; nothing to do\n");
		free(dtb);
		return r;
	}
	r = qsoc_init_gcc(dtb);
	free(dtb);
	if (r != OK) {
		log_warn(&dwc3_log, "no SDM660 clock controller (GCC) in the "
		    "tree, or not granted: %d\n", r);
		return r;
	}
	if (qsoc_gcc_phandle() != dt.gcc_ref) {
		log_warn(&dwc3_log, "clocks and resets name phandle %u, the GCC "
		    "is %u: not trusting them\n", dt.gcc_ref,
		    qsoc_gcc_phandle());
		return EINVAL;
	}

	log_info(&dwc3_log, "probe only (hw-net.md, step 2): nothing is "
	    "written\n");
	dt.glue->report(&dt);
	dt.phy->report(&dt);
	return OK;
}

static void
dwc3_signal(int signo)
{
	if (signo == SIGTERM)
		exit(0);
}

int
main(int argc, char **argv)
{
	message m;
	int ipc_status;

	env_setargs(argc, argv);
	sef_setcb_init_fresh(dwc3_init);
	sef_setcb_signal_handler(dwc3_signal);
	sef_startup();

	/* Nothing to serve yet: the network side comes with step 4. */
	for (;;)
		(void)sef_receive_status(ANY, &m, &ipc_status);
	return 0;
}
