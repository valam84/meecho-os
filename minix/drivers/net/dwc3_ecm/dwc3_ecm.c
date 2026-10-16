/*
 * dwc3_ecm: the driver's life (dwc3_ecm.h) - a network driver of
 * libnetdriver, whose interface lwip names "ecm0".
 *
 * Found by the wrapper's compatible ("qcom,dwc3"); a machine without one
 * (QEMU's virt, the CB2) gets a line saying so and nothing else.  The
 * clocks, resets and power domain the tree names must all come from the
 * SoC's clock controller (GCC), which qcom_soc maps; a tree that points
 * them elsewhere is not trusted.
 *
 * It reports what powers, clocks and resets the block has (step 2 of
 * port/clover/hw-net.md), and then - when the report finds the block
 * running, as a boot loader that used it leaves it - takes the core over
 * in device mode (step 3a) as a CDC Ethernet device (step 4, ecm.c).  The
 * core's events are read on every clock tick.  A block that is not running
 * is left alone: the report is printed and the driver leaves.
 *
 * Arguments: log=N (0 none .. 4 trace); probe=1 - report only, write
 * nothing, and leave.
 */

#include "dwc3_dev.h"
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
static int running;		/* the core is ours */

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
dwc3_init(unsigned int UNUSED(instance), netdriver_addr_t *hwaddr,
	uint32_t *caps, unsigned int *ticks)
{
	volatile uint8_t *core = NULL;
	void *dtb;
	long v;
	int r, rg, rp, probe = 0;

	if (env_parse("log", "d", 0, &v, LEVEL_NONE, LEVEL_TRACE) == EP_SET)
		dwc3_log.log_level = (int)v;
	if (env_parse("probe", "d", 0, &v, 0, 1) == EP_SET)
		probe = (int)v;
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

	if (probe)
		log_info(&dwc3_log, "probe=1: nothing is written\n");
	rg = dt.glue->report(&dt, &core);
	rp = dt.phy->report(&dt);
	if (probe)
		return ENXIO;
	if (rg != OK || rp != OK) {
		log_info(&dwc3_log, "not taking the USB block over: %s is not "
		    "running as a boot loader leaves it\n", rg != OK ?
		    "the wrapper or core" : "the PHY");
		return ENXIO;
	}

	/* hw-net.md, step 3a: the block as the boot loader left it. */
	log_info(&dwc3_log, "taking over the USB block the boot loader left "
	    "running; GCC and PHY untouched\n");
	dt.glue->peripheral();
	if ((r = dwc3_dev_start(core)) != OK) {
		log_warn(&dwc3_log, "device mode did not start: %d\n", r);
		return r;
	}
	running = 1;
	ecm_hwaddr(hwaddr);
	*caps = NDEV_CAP_BCAST | NDEV_CAP_MCAST;
	*ticks = 1;			/* the events, on every clock tick */
	return OK;
}

static void
dwc3_stop(void)
{
	if (running)
		dwc3_dev_stop();
	running = 0;
}

static void
dwc3_tick(void)
{
	if (running)
		dwc3_dev_poll();
}

static const struct netdriver dwc3_table = {
	.ndr_name	= "ecm",
	.ndr_init	= dwc3_init,
	.ndr_stop	= dwc3_stop,
	.ndr_recv	= ecm_recv,
	.ndr_send	= ecm_send,
	.ndr_get_link	= ecm_get_link,
	.ndr_tick	= dwc3_tick,
};

int
main(int argc, char **argv)
{
	env_setargs(argc, argv);
	netdriver_task(&dwc3_table);
	return 0;
}
