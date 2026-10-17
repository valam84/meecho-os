/*
 * Qualcomm's wrapper around the DWC3 core ("qcom,dwc3"): the GCC power
 * domain, block reset and clocks the tree names for it, and its QSCRATCH
 * registers (port/clover/hw-net.md, §3).
 *
 * The GCC is read through qcom_soc's looks and never written: this driver
 * takes the block over only as a boot loader left it running (hw-net.md,
 * step 3a).  The wrapper and the core are mapped and read only when the
 * power domain is on, no reset is held and every clock of the wrapper
 * runs - on this SoC a read of a block that is not clocked can stall the
 * bus, and the boot processor with it.  The one write here is the
 * session override of peripheral().
 */

#include "dwc3_ecm.h"
#include "dwc3_regs.h"
#include "qcom_soc.h"

static volatile uint8_t *qs;		/* QSCRATCH, once report() mapped it */

static uint32_t
rd32(volatile uint8_t *b, size_t off)
{
	return *(volatile uint32_t *)(b + off);
}

static void
wr32(volatile uint8_t *b, size_t off, uint32_t v)
{
	*(volatile uint32_t *)(b + off) = v;
}

/* "core: src 1 div 5" from a root's CMD_RCGR and CFG_RCGR. */
static int
root_text(char *buf, size_t size, const char *name, uint32_t cmd,
	uint32_t cfg)
{
	unsigned hid = cfg & 0x1f, src = (cfg >> 8) & 7, mode = (cfg >> 12) & 3;

	/* SRC_DIV n divides by (n + 1) / 2; 0 is the source itself. */
	return snprintf(buf, size, "%s: %s, src %u div %u%s%s", name,
	    (cmd & 0x80000000u) ? "root off" : "root on", src,
	    hid == 0 ? 1 : (hid + 1) / 2, (hid != 0 && !(hid & 1)) ? ".5" : "",
	    mode != 0 ? ", M/N" : "");
}

static int
qcom_report(const struct dwc3_dt *dt, volatile uint8_t **corep)
{
	char line[256], part[64];
	const char *name;
	volatile uint8_t *core;
	uint32_t v = 0, cmd, cfg;
	int n, st, pd_on = 1, held = 0, clk_on = 1;
	unsigned i;

	log_info(&dwc3_log, "%s (%s): qscratch 0x%llx, core 0x%llx irq %d; "
	    "%s, %s%s\n", dt->glue_node, dt->glue_compat,
	    (unsigned long long)dt->qscratch_pa,
	    (unsigned long long)dt->core_pa, dt->irq,
	    dt->dr_mode[0] ? dt->dr_mode : "dr_mode ?",
	    dt->max_speed[0] ? dt->max_speed : "speed ?",
	    dt->utmi_as_pipe ? ", utmi as pipe" : "");

	/* The power domain and the block resets. */
	n = 0;
	line[0] = '\0';
	if (dt->have_pd) {
		st = qsoc_gdsc_on(dt->pd, &v, &name);
		pd_on = st == 1;
		n += snprintf(line + n, sizeof(line) - n, "%s %s (0x%08x)",
		    st >= 0 && st <= 1 ? name : "power domain",
		    st == 1 ? "on" : st == 0 ? "off" : "unknown", v);
	}
	for (i = 0; i < dt->nrst && n < (int)sizeof(line); i++) {
		st = qsoc_bcr_asserted(dt->rst[i], &v, &name);
		if (st != 0)
			held = 1;
		n += snprintf(line + n, sizeof(line) - n, "%s%s %s",
		    n ? ", " : "", st >= 0 && st <= 1 ? name : "reset",
		    st == 1 ? "held" : st == 0 ? "released" : "unknown");
	}
	log_info(&dwc3_log, "%s\n", line);

	/* The clocks, by the tree's names, and the roots of the two that
	 * have their own rate. */
	n = snprintf(line, sizeof(line), "clocks:");
	for (i = 0; i < dt->nclk && n < (int)sizeof(line); i++) {
		st = dwc3_clk_state(&dt->clk[i], part, sizeof(part));
		if (st != 1)
			clk_on = 0;
		n += snprintf(line + n, sizeof(line) - n, " %s", part);
	}
	log_info(&dwc3_log, "%s\n", line);
	n = 0;
	line[0] = '\0';
	for (i = 0; i < dt->nclk && n < (int)sizeof(line); i++)
		if ((strcmp(dt->clk[i].name, "core") == 0 ||
		    strcmp(dt->clk[i].name, "mock_utmi") == 0) &&
		    qsoc_clk_root(dt->clk[i].id, &cmd, &cfg) == OK) {
			if (n)
				n += snprintf(line + n, sizeof(line) - n, "; ");
			n += root_text(line + n, sizeof(line) - n,
			    dt->clk[i].name, cmd, cfg);
		}
	if (n)
		log_info(&dwc3_log, "roots: %s\n", line);

	if (!pd_on || held || !clk_on) {
		log_info(&dwc3_log, "not reading the wrapper or the core: %s\n",
		    !pd_on ? "its power domain is off" : held ?
		    "a reset is held" : "a clock is off");
		return EBUSY;
	}
	if (dt->core_sz < DWC3_CORE_MIN_SIZE || dt->qscratch_sz <
	    QSCRATCH_SS_PHY_CTRL + 4) {
		log_warn(&dwc3_log, "reg of the core or wrapper too short\n");
		return EBUSY;
	}
	if ((qs = dwc3_map(dt->qscratch_pa, dt->qscratch_sz, "wrapper")) ==
	    NULL || (core = dwc3_map(dt->core_pa, dt->core_sz, "core")) == NULL)
		return EBUSY;
	v = rd32(core, DWC3_GSNPSID);
	log_info(&dwc3_log, "core GSNPSID 0x%08x%s, GCTL 0x%08x (%s), "
	    "GUSB2PHYCFG 0x%08x, GHWPARAMS3 0x%08x\n", v,
	    (v & DWC3_GSNPSID_MASK) == DWC3_GSNPSID_DWC3 ? "" : " (not DWC3?)",
	    rd32(core, DWC3_GCTL),
	    DWC3_GCTL_PRTCAP(rd32(core, DWC3_GCTL)) == 1 ? "host" :
	    DWC3_GCTL_PRTCAP(rd32(core, DWC3_GCTL)) == 2 ? "device" : "otg/?",
	    rd32(core, DWC3_GUSB2PHYCFG), rd32(core, DWC3_GHWPARAMS3));
	v = rd32(core, DWC3_DCTL);
	log_info(&dwc3_log, "device DCFG 0x%08x, DCTL 0x%08x (%s), DSTS "
	    "0x%08x, event buffer 0x%08x/%u\n", rd32(core, DWC3_DCFG), v,
	    (v & DWC3_DCTL_RUN_STOP) ? "running" : "stopped",
	    rd32(core, DWC3_DSTS), rd32(core, DWC3_GEVNTADRLO),
	    rd32(core, DWC3_GEVNTSIZ) & 0xffff);
	log_info(&dwc3_log, "qscratch GENERAL_CFG 0x%08x, HS_PHY_CTRL "
	    "0x%08x, SS_PHY_CTRL 0x%08x\n", rd32(qs, QSCRATCH_GENERAL_CFG),
	    rd32(qs, QSCRATCH_HS_PHY_CTRL), rd32(qs, QSCRATCH_SS_PHY_CTRL));
	if ((rd32(core, DWC3_GSNPSID) & DWC3_GSNPSID_MASK) != DWC3_GSNPSID_DWC3)
		return EBUSY;
	*corep = core;
	return OK;
}

/*
 * dwc3_qcom_vbus_override_enable(): the PHY's session is valid whatever
 * the wrapper's own VBUS input says, as Linux does when the tree says
 * "peripheral" and there is no extcon.
 */
static void
qcom_peripheral(void)
{
	if (qs == NULL)
		return;
	wr32(qs, QSCRATCH_SS_PHY_CTRL, rd32(qs, QSCRATCH_SS_PHY_CTRL) |
	    QSCRATCH_LANE0_PWR_PRESENT);
	wr32(qs, QSCRATCH_HS_PHY_CTRL, rd32(qs, QSCRATCH_HS_PHY_CTRL) |
	    QSCRATCH_UTMI_OTG_VBUS_VALID | QSCRATCH_SW_SESSVLD_SEL);
	(void)rd32(qs, QSCRATCH_HS_PHY_CTRL);
	log_info(&dwc3_log, "qscratch: session valid (HS_PHY_CTRL 0x%08x, "
	    "SS_PHY_CTRL 0x%08x)\n", rd32(qs, QSCRATCH_HS_PHY_CTRL),
	    rd32(qs, QSCRATCH_SS_PHY_CTRL));
}

/*
 * The same, off: in host mode Linux takes the override away
 * (dwc3_qcom_host_notifier()), and the downstream driver of the tablet
 * writes HS_PHY_CTRL only on its device path.
 */
static void
qcom_host(void)
{
	if (qs == NULL)
		return;
	wr32(qs, QSCRATCH_SS_PHY_CTRL, rd32(qs, QSCRATCH_SS_PHY_CTRL) &
	    ~QSCRATCH_LANE0_PWR_PRESENT);
	wr32(qs, QSCRATCH_HS_PHY_CTRL, rd32(qs, QSCRATCH_HS_PHY_CTRL) &
	    ~(QSCRATCH_UTMI_OTG_VBUS_VALID | QSCRATCH_SW_SESSVLD_SEL));
	(void)rd32(qs, QSCRATCH_HS_PHY_CTRL);
	log_info(&dwc3_log, "qscratch: no session override (HS_PHY_CTRL "
	    "0x%08x, SS_PHY_CTRL 0x%08x)\n", rd32(qs, QSCRATCH_HS_PHY_CTRL),
	    rd32(qs, QSCRATCH_SS_PHY_CTRL));
}

const struct dwc3_glue_ops dwc3_qcom_glue = {
	.compatible	= "qcom,dwc3",
	.report		= qcom_report,
	.peripheral	= qcom_peripheral,
	.host		= qcom_host
};
