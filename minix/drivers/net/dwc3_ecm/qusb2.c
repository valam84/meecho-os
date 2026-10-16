/*
 * The QUSB2 PHY of the SDM660 ("qcom,sdm660-qusb2-phy"): Linux
 * drivers/phy/qualcomm/phy-qcom-qusb2.c, sdm660_phy_cfg - the msm8996
 * register layout and table, a PLL_TEST register, a differential reference
 * clock (port/clover/hw-net.md, §3).
 *
 * At this milestone it only reports: its clocks, reset and supplies, the
 * tuning fuse in the QFPROM, and - when its interface clock (cfg_ahb) runs
 * and its reset is released - its PLL status and power-down register.
 */

#include "dwc3_ecm.h"
#include "qcom_soc.h"

#define QUSB2_PLL_TEST		0x04	/* bit7 CLK_REF_SEL: single-ended */
#define QUSB2_PLL_STATUS	0x38	/* bit5 PLL_LOCKED */
#define QUSB2_PLL_LOCKED	0x20
#define QUSB2_TUNE1		0x80
#define QUSB2_TUNE2		0x84
#define QUSB2_POWERDOWN		0xb4	/* bit0 POWER_DOWN */
#define QUSB2_MIN_SIZE		(QUSB2_POWERDOWN + 4)

static uint32_t
rd32(volatile uint8_t *b, size_t off)
{
	return *(volatile uint32_t *)(b + off);
}

/* The byte of the fuse cell, read as the aligned word around it. */
static void
fuse_report(const struct dwc3_dt *dt)
{
	volatile uint8_t *f;
	uint64_t off = dt->trim_off & ~(uint64_t)3;
	uint8_t byte;
	unsigned trim;

	if (dt->trim_phandle == 0) {
		log_info(&dwc3_log, "phy: no tuning fuse in the tree\n");
		return;
	}
	if ((f = dwc3_map(dt->fuse_base, dt->fuse_size, "QFPROM")) == NULL)
		return;
	byte = (uint8_t)(rd32(f, (size_t)off) >> (8 * (dt->trim_off & 3)));
	trim = (byte >> dt->trim_bit) & ((1u << dt->trim_nbits) - 1);
	log_info(&dwc3_log, "phy: fuse 0x%llx = 0x%02x, bits %u..%u: trim %u%s\n",
	    (unsigned long long)(dt->fuse_base + dt->trim_off), byte,
	    dt->trim_bit, dt->trim_bit + dt->trim_nbits - 1, trim,
	    trim == 0 ? " (not fused: the table's value stays)" : "");
}

static void
qusb2_report(const struct dwc3_dt *dt)
{
	char line[256], part[64];
	const char *name;
	volatile uint8_t *p;
	uint32_t v = 0;
	int n, st, ahb_on = 0, held = 0;
	unsigned i;

	log_info(&dwc3_log, "phy %s at 0x%llx; supplies vdd %s, vdda-pll %s, "
	    "vdda-phy-dpdm %s\n", dt->phy_compat,
	    (unsigned long long)dt->phy_pa,
	    dt->supply[0][0] ? dt->supply[0] : "-",
	    dt->supply[1][0] ? dt->supply[1] : "-",
	    dt->supply[2][0] ? dt->supply[2] : "-");

	n = snprintf(line, sizeof(line), "phy clocks:");
	for (i = 0; i < dt->nphyclk && n < (int)sizeof(line); i++) {
		st = dwc3_clk_state(&dt->phy_clk[i], part, sizeof(part));
		if (strcmp(dt->phy_clk[i].name, "cfg_ahb") == 0 && st == 1)
			ahb_on = 1;
		n += snprintf(line + n, sizeof(line) - n, " %s", part);
	}
	if (dt->have_phy_rst && n < (int)sizeof(line)) {
		st = qsoc_bcr_asserted(dt->phy_rst, &v, &name);
		held = st != 0;
		n += snprintf(line + n, sizeof(line) - n, "; %s %s",
		    st >= 0 && st <= 1 ? name : "reset",
		    st == 1 ? "held" : st == 0 ? "released" : "unknown");
	}
	log_info(&dwc3_log, "%s\n", line);

	fuse_report(dt);

	if (!ahb_on || held) {
		log_info(&dwc3_log, "not reading the phy: %s\n", !ahb_on ?
		    "cfg_ahb is off" : "its reset is held");
		return;
	}
	if (dt->phy_sz < QUSB2_MIN_SIZE) {
		log_warn(&dwc3_log, "reg of the phy too short\n");
		return;
	}
	if ((p = dwc3_map(dt->phy_pa, dt->phy_sz, "phy")) == NULL)
		return;
	v = rd32(p, QUSB2_PLL_STATUS);
	log_info(&dwc3_log, "phy PLL_STATUS 0x%02x (%s), POWERDOWN 0x%02x, "
	    "PLL_TEST 0x%02x, TUNE1 0x%02x, TUNE2 0x%02x\n", v & 0xff,
	    (v & QUSB2_PLL_LOCKED) ? "locked" : "not locked",
	    rd32(p, QUSB2_POWERDOWN) & 0xff, rd32(p, QUSB2_PLL_TEST) & 0xff,
	    rd32(p, QUSB2_TUNE1) & 0xff, rd32(p, QUSB2_TUNE2) & 0xff);
}

const struct dwc3_phy_ops qusb2_phy = {
	.compatible	= "qcom,sdm660-qusb2-phy",
	.report		= qusb2_report
};
