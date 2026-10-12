/*
 * The Rockchip RK3566/RK3568 around the display: power, clocks, the pixel
 * PLL, pins and two GRF bits.
 *
 * None of this is in a VOP2 or a DesignWare document, and all of it was read
 * off the vendor system on the board before it was written
 * (port/cb2-display/ref-720p-forced.txt and registers.md).  The numbers are
 * Linux's for the same clocks (clk-rk3568.c, pm_domains.c and
 * dw_hdmi-rockchip.c of 6.1.115-btt-rockchip64).
 *
 * The one thing here that is not a matter of opening gates is the pixel
 * clock.  The display controller's dclk for video port 0 and the HDMI PHY's
 * reference are both taken from HPLL, a PLL in the PMU's clock unit that
 * exists for exactly this and that nothing else on the board uses, and on
 * the board both run at the pixel clock itself - 74.25 MHz at 720p, divider
 * one.  So setting a mode is setting HPLL, and the PLL's dividers are
 * computed here rather than looked up: a table would only cover the modes
 * someone thought of.
 *
 * The bootloader of this board brings up no display (its configuration has
 * CONFIG_VIDEO unset), so nothing here can lean on what it left - every
 * register this file depends on, it writes.
 */

#include <minix/drivers.h>
#include <minix/fdt.h>
#include <minix/log.h>
#include <minix/syslib.h>
#include <minix/sysutil.h>

#include <sys/mman.h>

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "display.h"

/* The main clock unit. */
#define CRU_CLKSEL_CON(n)	(0x100 + 4 * (n))
#define CRU_CLKGATE_CON(n)	(0x300 + 4 * (n))

#define CLKSEL_VO		37	/* aclk_vo mux 1:0, hclk_vo 11:8, pclk_vo 15:12 */
#define CLKSEL_VOP_PRE		38	/* aclk_vop_pre mux 7:6, div 4:0 */
#define CLKSEL_DCLK_VOP0	39	/* dclk_vop0 mux 11:10, div 7:0 */
#define  DCLK_SRC_HPLL		0

#define CLKGATE_VO		20
#define  GATE_ACLK_VO		(1u << 0)
#define  GATE_HCLK_VO		(1u << 1)
#define  GATE_PCLK_VO		(1u << 2)
#define  GATE_ACLK_VOP_PRE	(1u << 6)
#define  GATE_ACLK_VOP		(1u << 8)
#define  GATE_HCLK_VOP		(1u << 9)
#define  GATE_DCLK_VOP0		(1u << 10)
#define CLKGATE_VO_HDMI		21
#define  GATE_PCLK_HDMI_HOST	(1u << 3)
#define  GATE_CLK_HDMI_SFR	(1u << 4)
#define  GATE_CLK_HDMI_CEC	(1u << 5)

/* The PMU's clock unit, where HPLL lives. */
#define PMUCRU_HPLL_CON0	0x40	/* bypass 15, postdiv1 14:12, fbdiv 11:0 */
#define PMUCRU_HPLL_CON1	0x44	/* dsmpd 12, lock 10, postdiv2 8:6, refdiv 5:0 */
#define  PLL_PWRDOWN_SHIFT	13
#define  PLL_LOCK		(1u << 10)
#define PMUCRU_HPLL_CON2	0x48	/* fraction 23:0, not write-masked */
#define PMUCRU_MODE_CON0	0x80	/* HPLL 3:2: 0 slow (24 MHz), 1 normal */
#define  HPLL_MODE_SHIFT	2
#define  PLL_MODE_SLOW		0
#define  PLL_MODE_NORMAL	1
#define PMUCRU_CLKSEL_CON(n)	(0x100 + 4 * (n))
#define CLKSEL_HDMI_REF		8	/* bit 7: 0 hpll, 1 hpll_ph0 */

/* The PMU: power domains and the bus idle handshake. */
#define PMU_IDLE_REQ		0x50
#define PMU_IDLE_ST		0x68
#define PMU_PWR_STATUS		0x98
#define PMU_PWR_CON		0xa0
#define RK3568_PD_VO		9
#define  PD_VO_PWR		(1u << 7)
#define  PD_VO_IDLE		(1u << 4)

/* The GRF. */
#define GRF_IOMUX(bank, pin)	(((bank) - 1) * 0x20 + ((pin) / 4) * 4)
#define GRF_VO_CON1		0x364
#define  VO_CON1_HDMI_SDAIN	(1u << 15)
#define  VO_CON1_HDMI_SCLIN	(1u << 14)

#define XIN_KHZ			24000

/* PLL limits: VCO 950 MHz..3.8 GHz, the post dividers 1..7 each. */
#define PLL_VCO_MIN_KHZ		950000
#define PLL_VCO_MAX_KHZ		3800000

static int
map(struct disp_regs *r, const char *what)
{
	void *v;

	if (r->base == 0) {
		log_warn(&fb_log, "the tree names no %s\n", what);
		return ENXIO;
	}
	if ((v = vm_map_phys(SELF, (void *)r->base, r->size)) == MAP_FAILED) {
		log_warn(&fb_log, "cannot map the %s at 0x%lx; not granted by "
		    "RS?\n", what, (unsigned long)r->base);
		return EPERM;
	}
	r->v = v;
	return OK;
}

int
rk_vo_map(struct disp_info *d)
{
	int r;

	if ((r = map(&d->cru, "clock unit")) != OK ||
	    (r = map(&d->pmucru, "PMU clock unit")) != OK ||
	    (r = map(&d->grf, "GRF")) != OK)
		return r;

	/* A machine whose firmware left the domain on needs no PMU. */
	if (d->pmu.base != 0 && map(&d->pmu, "PMU") != OK)
		d->pmu.v = NULL;
	return OK;
}

/*
 * The VO power domain - both parts sit in it.  Read first and say what was
 * read; on the board Linux has it on, and with no display in the loader the
 * question of what reset leaves is answered by the first run.  The sequence
 * is the vendor's (and xhci_rk.c's for PD_PIPE): power, then let the bus
 * out of idle.
 */
int
rk_vo_power(struct disp_info *d)
{
	unsigned spins;

	if (d->pmu.v == NULL || d->power_domain != RK3568_PD_VO) {
		log_info(&fb_log, "power domain %d: not one this driver "
		    "switches; assuming it is on\n", d->power_domain);
		return OK;
	}

	if (!(disp_rd(&d->pmu, PMU_PWR_STATUS) & PD_VO_PWR) &&
	    !(disp_rd(&d->pmu, PMU_IDLE_ST) & PD_VO_IDLE)) {
		log_info(&fb_log, "power domain VO is on\n");
		return OK;
	}

	log_info(&fb_log, "power domain VO is off (status 0x%x, idle 0x%x); "
	    "switching it on\n", disp_rd(&d->pmu, PMU_PWR_STATUS),
	    disp_rd(&d->pmu, PMU_IDLE_ST));

	disp_wr(&d->pmu, PMU_PWR_CON, PD_VO_PWR << 16);
	for (spins = 0; spins < 10000; spins++) {
		if (!(disp_rd(&d->pmu, PMU_PWR_STATUS) & PD_VO_PWR))
			break;
		micro_delay(10);
	}
	disp_wr(&d->pmu, PMU_IDLE_REQ, PD_VO_IDLE << 16);
	for (spins = 0; spins < 10000; spins++) {
		if (!(disp_rd(&d->pmu, PMU_IDLE_ST) & PD_VO_IDLE))
			break;
		micro_delay(10);
	}
	if ((disp_rd(&d->pmu, PMU_PWR_STATUS) & PD_VO_PWR) ||
	    (disp_rd(&d->pmu, PMU_IDLE_ST) & PD_VO_IDLE)) {
		log_warn(&fb_log, "power domain VO did not come up\n");
		return EIO;
	}
	log_info(&fb_log, "power domain VO is on\n");
	return OK;
}

struct pll {
	unsigned refdiv, fbdiv, postdiv1, postdiv2;
	unsigned khz;
};

/*
 * The integer-mode dividers closest to the wanted rate: out = 24 MHz /
 * refdiv * fbdiv / postdiv1 / postdiv2, with the VCO in its range.  Exact
 * for every CEA rate this driver has met (74.25 MHz is 2/99/4/4, the
 * board's own; 148.5 MHz is 1/99/4/4).  The fractional mode would hit any
 * rate, and is not needed while the monitor's tolerance is half a percent.
 */
static int
pll_solve(unsigned khz, struct pll *best)
{
	unsigned refdiv, fbdiv, pd1, pd2, vco, out, err, best_err = ~0u;

	for (pd1 = 1; pd1 <= 7; pd1++)
		for (pd2 = 1; pd2 <= pd1; pd2++)
			for (refdiv = 1; refdiv <= 24; refdiv++) {
				fbdiv = (unsigned)(((uint64_t)khz * pd1 * pd2 *
				    refdiv + XIN_KHZ / 2) / XIN_KHZ);
				if (fbdiv < 16 || fbdiv > 2500)
					continue;
				vco = (unsigned)((uint64_t)XIN_KHZ * fbdiv /
				    refdiv);
				if (vco < PLL_VCO_MIN_KHZ ||
				    vco > PLL_VCO_MAX_KHZ)
					continue;
				out = vco / (pd1 * pd2);
				err = out > khz ? out - khz : khz - out;
				if (err < best_err) {
					best_err = err;
					best->refdiv = refdiv;
					best->fbdiv = fbdiv;
					best->postdiv1 = pd1;
					best->postdiv2 = pd2;
					best->khz = out;
				}
			}

	/* Half a percent is what a sink must accept. */
	if (best_err == ~0u || best_err * 200 > khz)
		return EINVAL;
	return OK;
}

static int
hpll_set(struct disp_info *d, unsigned khz, unsigned *got)
{
	struct pll p;
	unsigned spins;

	memset(&p, 0, sizeof(p));
	if (pll_solve(khz, &p) != OK) {
		log_warn(&fb_log, "HPLL cannot make %u kHz\n", khz);
		return EINVAL;
	}

	/*
	 * Run the consumers from the crystal while the PLL relocks, as the
	 * vendor kernel does, then back.  Every register here is
	 * write-masked except the fraction, which is zero in integer mode.
	 */
	disp_wr(&d->pmucru, PMUCRU_MODE_CON0,
	    RK_HIWORD(PLL_MODE_SLOW, 2, HPLL_MODE_SHIFT));
	disp_wr(&d->pmucru, PMUCRU_HPLL_CON0,
	    RK_HIWORD(0, 1, 15) | RK_HIWORD(p.postdiv1, 3, 12) |
	    RK_HIWORD(p.fbdiv, 12, 0));
	disp_wr(&d->pmucru, PMUCRU_HPLL_CON1,
	    RK_HIWORD(0, 1, PLL_PWRDOWN_SHIFT) | RK_HIWORD(1, 1, 12) |
	    RK_HIWORD(p.postdiv2, 3, 6) | RK_HIWORD(p.refdiv, 6, 0));
	disp_wr(&d->pmucru, PMUCRU_HPLL_CON2,
	    disp_rd(&d->pmucru, PMUCRU_HPLL_CON2) & ~0xffffffu);

	for (spins = 0; spins < 1000; spins++) {
		if (disp_rd(&d->pmucru, PMUCRU_HPLL_CON1) & PLL_LOCK)
			break;
		micro_delay(10);
	}
	if (!(disp_rd(&d->pmucru, PMUCRU_HPLL_CON1) & PLL_LOCK)) {
		log_warn(&fb_log, "HPLL did not lock (con1 0x%08x)\n",
		    disp_rd(&d->pmucru, PMUCRU_HPLL_CON1));
		return EIO;
	}
	disp_wr(&d->pmucru, PMUCRU_MODE_CON0,
	    RK_HIWORD(PLL_MODE_NORMAL, 2, HPLL_MODE_SHIFT));

	log_info(&fb_log, "HPLL %u kHz (asked %u): refdiv %u fbdiv %u "
	    "postdiv %u/%u\n", p.khz, khz, p.refdiv, p.fbdiv, p.postdiv1,
	    p.postdiv2);
	*got = p.khz;
	return OK;
}

int
rk_vo_clocks(struct disp_info *d, unsigned pixel_khz, unsigned *got_khz)
{
	int r;

	/*
	 * The bus and core clocks, at the board's rates: aclk_vo from the
	 * GPLL's 297 MHz tap, hclk_vo half of it, pclk_vo a quarter;
	 * aclk_vop 1188/3 = 396 MHz.
	 */
	disp_wr(&d->cru, CRU_CLKSEL_CON(CLKSEL_VO),
	    RK_HIWORD(0, 2, 0) | RK_HIWORD(1, 4, 8) | RK_HIWORD(3, 4, 12));
	disp_wr(&d->cru, CRU_CLKSEL_CON(CLKSEL_VOP_PRE),
	    RK_HIWORD(1, 2, 6) | RK_HIWORD(2, 5, 0));

	/* The pixel clock: HPLL at the rate itself, dclk divides by one. */
	if ((r = hpll_set(d, pixel_khz, got_khz)) != OK)
		return r;
	disp_wr(&d->cru, CRU_CLKSEL_CON(CLKSEL_DCLK_VOP0),
	    RK_HIWORD(DCLK_SRC_HPLL, 2, 10) | RK_HIWORD(0, 8, 0));
	disp_wr(&d->pmucru, PMUCRU_CLKSEL_CON(CLKSEL_HDMI_REF),
	    RK_HIWORD(0, 1, 7));

	/* And the gates: a set bit is a stopped clock. */
	disp_wr(&d->cru, CRU_CLKGATE_CON(CLKGATE_VO),
	    (GATE_ACLK_VO | GATE_HCLK_VO | GATE_PCLK_VO | GATE_ACLK_VOP_PRE |
	    GATE_ACLK_VOP | GATE_HCLK_VOP | GATE_DCLK_VOP0) << 16);
	disp_wr(&d->cru, CRU_CLKGATE_CON(CLKGATE_VO_HDMI),
	    (GATE_PCLK_HDMI_HOST | GATE_CLK_HDMI_SFR | GATE_CLK_HDMI_CEC) << 16);

	/* The encoder's DDC inputs, which the GRF gates. */
	disp_wr(&d->grf, GRF_VO_CON1,
	    (VO_CON1_HDMI_SDAIN | VO_CON1_HDMI_SCLIN) << 16 |
	    VO_CON1_HDMI_SDAIN | VO_CON1_HDMI_SCLIN);
	return OK;
}

/*
 * The encoder's pins, from the tree: "pinctrl-0" lists pin groups by
 * phandle, and each group's "rockchip,pins" is <bank pin function config>
 * quads.  DDC clock and data and CEC on this board, all in bank 4, all
 * function 1; the TMDS lines and hot plug are dedicated and need nothing.
 * Bank 0 lives in the PMU's GRF, which this driver is not given - it says
 * so instead of writing the wrong file.
 */
struct pin_search {
	u32_t phandle;
	struct disp_info *d;
	int found;
};

static int
pin_group(void *cookie, int UNUSED(depth), const char *UNUSED(name),
	const struct fdt_node *node)
{
	struct pin_search *s = cookie;
	const void *p;
	unsigned len, i, bank, pin, func;

	if ((p = fdt_getprop(node, "phandle", &len)) == NULL || len != 4 ||
	    (u32_t)fdt_read_cells(p, 1) != s->phandle)
		return 0;
	s->found = 1;
	if ((p = fdt_getprop(node, "rockchip,pins", &len)) == NULL)
		return 1;
	for (i = 0; i + 16 <= len; i += 16) {
		bank = (unsigned)fdt_read_cells((const char *)p + i, 1);
		pin = (unsigned)fdt_read_cells((const char *)p + i + 4, 1);
		func = (unsigned)fdt_read_cells((const char *)p + i + 8, 1);
		if (bank < 1 || bank > 4 || pin > 31) {
			log_warn(&fb_log, "pin %u-%u: not in this driver's "
			    "GRF; left alone\n", bank, pin);
			continue;
		}
		disp_wr(&s->d->grf, GRF_IOMUX(bank, pin),
		    RK_HIWORD(func, 4, (pin % 4) * 4));
		log_debug(&fb_log, "pin GPIO%u_%c%u -> function %u\n", bank,
		    'A' + pin / 8, pin % 8, func);
	}
	return 1;
}

void
rk_vo_pins(struct disp_info *d, const void *dtb)
{
	struct pin_search s;
	unsigned i;

	for (i = 0; i < d->npinctrl; i++) {
		s.phandle = d->pinctrl[i];
		s.d = d;
		s.found = 0;
		fdt_walk(dtb, pin_group, &s);
		if (!s.found)
			log_warn(&fb_log, "pin group %u is not in the tree\n",
			    s.phandle);
	}
}

/* What the board's Linux had in these registers at 1280x720. */
static const struct {
	const char *name;
	unsigned block;			/* 0 cru, 1 pmucru, 2 grf */
	unsigned off;
	uint32_t val, mask;
} reference[] = {
	{ "CLKSEL_CON37 (vo)",	   0, CRU_CLKSEL_CON(CLKSEL_VO), 0x3100, 0xffff },
	{ "CLKSEL_CON38 (vop)",	   0, CRU_CLKSEL_CON(CLKSEL_VOP_PRE), 0x0042,
	    0x00ff },
	{ "CLKSEL_CON39 (dclk0)",  0, CRU_CLKSEL_CON(CLKSEL_DCLK_VOP0), 0x0000,
	    0x0cff },
	{ "CLKGATE_CON20",	   0, CRU_CLKGATE_CON(CLKGATE_VO), 0x3800,
	    0x0747 },
	{ "CLKGATE_CON21",	   0, CRU_CLKGATE_CON(CLKGATE_VO_HDMI), 0x03c7,
	    0x0038 },
	{ "HPLL_CON0",		   1, PMUCRU_HPLL_CON0, 0x4063, 0xffff },
	{ "HPLL_CON1",		   1, PMUCRU_HPLL_CON1, 0x1502, 0x35ff },
	{ "MODE_CON0",		   1, PMUCRU_MODE_CON0, 0x0005, 0x000c },
	{ "PMU CLKSEL_CON8",	   1, PMUCRU_CLKSEL_CON(CLKSEL_HDMI_REF), 0x050f,
	    0x0080 },
	{ "GRF_VO_CON1",	   2, GRF_VO_CON1, 0xc000, 0xc000 },
	{ "GPIO4C_IOMUX_H",	   2, GRF_IOMUX(4, 20), 0x1000, 0xf000 },
	{ "GPIO4D_IOMUX_L",	   2, GRF_IOMUX(4, 24), 0x0011, 0x00ff },
};

void
rk_vo_report(struct disp_info *d)
{
	const struct disp_regs *b;
	unsigned i;
	uint32_t v;

	for (i = 0; i < sizeof(reference) / sizeof(reference[0]); i++) {
		b = reference[i].block == 0 ? &d->cru :
		    reference[i].block == 1 ? &d->pmucru : &d->grf;
		v = disp_rd(b, reference[i].off);
		log_info(&fb_log, "soc %-21s 0x%03x = 0x%08x (reference "
		    "0x%04x under 0x%04x)%s\n", reference[i].name,
		    reference[i].off, v, reference[i].val, reference[i].mask,
		    ((v ^ reference[i].val) & reference[i].mask) ? " DIFFERS" :
		    "");
	}
	if (d->pmu.v != NULL)
		log_info(&fb_log, "soc PMU power status 0x%08x, idle 0x%08x "
		    "(VO is bit 7 and bit 4; set means off)\n",
		    disp_rd(&d->pmu, PMU_PWR_STATUS),
		    disp_rd(&d->pmu, PMU_IDLE_ST));
}
