/*
 * The RK3566/RK3568 around the GPU: its power domain, its clock gates and
 * its reset lines.  The only file of this driver that knows the SoC.
 *
 * The numbers are the vendor kernel's for the same blocks (pm_domains.c,
 * clk-rk3568.c and the dt-bindings of 6.1 rkr5.1, the line the board runs),
 * and what each register holds on the working system is in
 * port/cb2-gpu/.  Three blocks:
 *
 *   PMU   PD_GPU, domain 7: power bit 0 in PWR_CON/PWR_STATUS, bit 1 in
 *         the bus idle handshake (IDLE_REQ, IDLE_ACK, IDLE_ST).  A set bit
 *         means off, or idle.
 *   CRU   CLKSEL_CON(6) - the GPU's source clock: mux 7:6 over mpll, gpll,
 *         cpll, npll, divider 3:0, and bit 11 choosing between that and
 *         the GPU's own PVTPLL; CLKGATE_CON(2) - bit 0 the source, bit 3
 *         clk_gpu (the tree's "bus", CLK_GPU = 27); SOFTRST_CON(2) - bits
 *         0..2 the core and its two bus interfaces.
 *
 * The source clock is the one the tree calls "gpu", and it is the secure
 * firmware's: an SCMI clock, whose rate BL31 sets and Linux only asks for.
 * The muxes are read-only to Linux for that reason.  This driver reads
 * them to say what rate the GPU runs at and changes nothing there; the
 * rate it boots with is enough to bring the part up, and choosing another
 * is stage 11.8, together with the voltage it needs.
 *
 * Order, as the vendor kernel does it: the clocks first, because the power
 * domain's own bus clocks (aclk_gpu_pre, pclk_gpu_pre) hang off the
 * source and must run while the domain switches; then the domain, power
 * before the bus is let out of idle; then the resets.  Every step reads
 * first and says what it found, and does nothing when the work is done
 * already - which on a board that came through Linux it will be.
 */

#include <minix/drivers.h>
#include <minix/syslib.h>
#include <minix/sysutil.h>

#include <sys/mman.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>

#include "mali.h"

/* The clock unit. */
#define CRU_PLL_CON(pll, n)	((pll) * 0x20 + 4 * (n))
#define  PLL_CON0_BYPASS	(1u << 15)
#define CRU_MODE_CON0		0xc0
#define CRU_CLKSEL_CON(n)	(0x100 + 4 * (n))
#define CRU_CLKGATE_CON(n)	(0x300 + 4 * (n))
#define CRU_SOFTRST_CON(n)	(0x400 + 4 * (n))

#define CLKSEL_GPU		6
#define  GPU_SRC_DIV(x)		((x) & 0xf)
#define  GPU_SRC_MUX(x)		(((x) >> 6) & 0x3)
#define  GPU_ACLK_DIV(x)	(((x) >> 8) & 0x3)
#define  GPU_PRE_MUX_PVTPLL(x)	(((x) >> 11) & 0x1)
#define  GPU_PCLK_DIV(x)	(((x) >> 12) & 0xf)
#define CLKGATE_GPU		2
#define  GATE_CLK_GPU_SRC	(1u << 0)
#define  GATE_CLK_GPU		(1u << 3)
#define SOFTRST_GPU		2
#define  SRST_GPU_ALL		0x7u	/* core, AXI interface, APB interface */

/* The clock ids of the tree that this file knows the gate of. */
#define RK3568_CLK_GPU		27

/* The PMU. */
#define PMU_IDLE_REQ		0x50
#define PMU_IDLE_ACK		0x60
#define PMU_IDLE_ST		0x68
#define PMU_PWR_STATUS		0x98
#define PMU_PWR_CON		0xa0
#define RK3568_PD_GPU		7
#define  PD_GPU_PWR		(1u << 0)
#define  PD_GPU_IDLE		(1u << 1)

#define XIN_KHZ			24000

/* The PLLs that can feed the GPU, by their place in the CRU. */
static const struct {
	const char *name;
	int index;			/* PLL_CON block, -1 not in this CRU */
	unsigned mode_shift;		/* in MODE_CON0 */
} gpu_src[4] = {
	{ "mpll", -1, 0 },		/* not one of the CRU's PLLs */
	{ "gpll", 2, 6 },
	{ "cpll", 3, 4 },
	{ "npll", 4, 10 },
};

static int
map(struct mali_regs *r, const char *what)
{
	void *v;

	if (r->base == 0) {
		log_warn(&mali_log, "the tree names no %s\n", what);
		return ENXIO;
	}
	if ((v = vm_map_phys(SELF, (void *)r->base, r->size)) == MAP_FAILED) {
		log_warn(&mali_log, "cannot map the %s at 0x%lx; not granted "
		    "by RS?\n", what, (unsigned long)r->base);
		return EPERM;
	}
	r->v = (vir_bytes)v;
	return OK;
}

int
mali_rk_map(void)
{
	struct mali_info *info = &mali.info;
	int r;

	if ((r = map(&info->gpu, "GPU")) != OK ||
	    (r = map(&info->cru, "clock unit")) != OK ||
	    (r = map(&info->pmu, "PMU")) != OK)
		return r;

	/*
	 * The gate this file opens is the one the tree points at only if
	 * the "bus" clock is in this CRU and is CLK_GPU.  Anything else is
	 * a tree this driver was not written against, and opening the gate
	 * it knows would be opening somebody else's.
	 */
	if (info->bus_clk_provider != info->cru.base ||
	    info->bus_clk_id != RK3568_CLK_GPU) {
		log_warn(&mali_log, "the \"bus\" clock is %u of 0x%lx, not "
		    "CLK_GPU (%u) of the clock unit at 0x%lx; this driver "
		    "knows no other\n", info->bus_clk_id,
		    (unsigned long)info->bus_clk_provider, RK3568_CLK_GPU,
		    (unsigned long)info->cru.base);
		return ENXIO;
	}
	if (info->power_domain != RK3568_PD_GPU) {
		log_warn(&mali_log, "power domain %d is not PD_GPU (%d); "
		    "this driver knows no other\n", info->power_domain,
		    RK3568_PD_GPU);
		return ENXIO;
	}
	return OK;
}

/* A PLL's output in kHz, or 0 if it is not one this file can read. */
static unsigned
pll_khz(int index, unsigned mode_shift)
{
	const struct mali_regs *cru = &mali.info.cru;
	uint32_t con0, con1, con2, mode;
	unsigned refdiv, fbdiv, pd1, pd2;
	uint64_t khz;

	if (index < 0)
		return 0;
	mode = (mali_rd(cru, CRU_MODE_CON0) >> mode_shift) & 0x3;
	if (mode == 0)
		return XIN_KHZ;		/* slow mode: the oscillator */
	if (mode != 1)
		return 0;		/* deep slow: 32 kHz */

	con0 = mali_rd(cru, CRU_PLL_CON(index, 0));
	con1 = mali_rd(cru, CRU_PLL_CON(index, 1));
	con2 = mali_rd(cru, CRU_PLL_CON(index, 2));
	if (con0 & PLL_CON0_BYPASS)
		return XIN_KHZ;
	fbdiv = con0 & 0xfff;
	pd1 = (con0 >> 12) & 0x7;
	refdiv = con1 & 0x3f;
	pd2 = (con1 >> 6) & 0x7;
	if (refdiv == 0 || pd1 == 0 || pd2 == 0)
		return 0;
	khz = (uint64_t)XIN_KHZ * fbdiv;
	if (!(con1 & (1u << 12)))	/* fractional mode: dsmpd clear */
		khz += ((uint64_t)XIN_KHZ * (con2 & 0xffffff)) >> 24;
	return (unsigned)(khz / refdiv / pd1 / pd2);
}

/* Say what the SoC has around the GPU right now. */
void
mali_rk_report(const char *when)
{
	const struct mali_regs *cru = &mali.info.cru;
	const struct mali_regs *pmu = &mali.info.pmu;
	uint32_t sel, gate, rst, pwr, idle, ack;
	unsigned src, khz;
	char rate[48];

	sel = mali_rd(cru, CRU_CLKSEL_CON(CLKSEL_GPU));
	gate = mali_rd(cru, CRU_CLKGATE_CON(CLKGATE_GPU));
	rst = mali_rd(cru, CRU_SOFTRST_CON(SOFTRST_GPU));
	pwr = mali_rd(pmu, PMU_PWR_STATUS);
	idle = mali_rd(pmu, PMU_IDLE_ST);
	ack = mali_rd(pmu, PMU_IDLE_ACK);

	src = GPU_SRC_MUX(sel);
	khz = pll_khz(gpu_src[src].index, gpu_src[src].mode_shift);
	rate[0] = '\0';
	if (GPU_PRE_MUX_PVTPLL(sel))
		snprintf(rate, sizeof(rate), " (unused: the core runs off "
		    "its PVTPLL)");
	else if (khz != 0)
		snprintf(rate, sizeof(rate), " = %u kHz",
		    khz / (GPU_SRC_DIV(sel) + 1));

	log_info(&mali_log, "%s: CLKSEL_CON6 0x%08x: %s/%u%s, aclk /%u, "
	    "pclk /%u\n", when, sel, gpu_src[src].name, GPU_SRC_DIV(sel) + 1,
	    rate, GPU_ACLK_DIV(sel) + 1, GPU_PCLK_DIV(sel) + 1);
	log_info(&mali_log, "%s: gates 0x%08x (source %s, clk_gpu %s), "
	    "resets 0x%08x (%s), PD_GPU %s%s\n", when, gate,
	    (gate & GATE_CLK_GPU_SRC) ? "shut" : "open",
	    (gate & GATE_CLK_GPU) ? "shut" : "open", rst,
	    (rst & SRST_GPU_ALL) ? "held" : "released",
	    (pwr & PD_GPU_PWR) ? "off" : "on",
	    (idle & PD_GPU_IDLE) || (ack & PD_GPU_IDLE) ? ", bus idle" : "");
}

/*
 * The clock under the core and under its registers.
 *
 * The register interface runs off the same mux as the core (pclk_gpu_pre
 * divides clk_gpu_pre_mux), so the mux decides whether a read of the GPU
 * answers at all.  Its other input is the GPU's PVTPLL - a ring oscillator
 * the firmware sets up when asked over SCMI for certain rates, and that
 * this driver can neither see running nor ask for.  The vendor system
 * leaves the mux on it (CLKSEL_CON6 0x4951 in port/cb2-display's snapshot,
 * gpll/2 behind it).  So a PVTPLL found here is not gambled on: the mux is
 * moved to the PLL path, with the source divided down to no more than
 * MALI_SAFE_KHZ - an operating point the tree allows at the lowest voltage
 * it lists (400 MHz at 0.85 V; vdd_gpu boots at 0.9 V) - and the result is
 * said.  A source this file cannot compute is refused.  Choosing a faster
 * rate is stage 11.8, together with the voltage it needs.
 */
#define MALI_SAFE_KHZ		400000

static int
clock_source(void)
{
	const struct mali_regs *cru = &mali.info.cru;
	uint32_t sel = mali_rd(cru, CRU_CLKSEL_CON(CLKSEL_GPU));
	unsigned src = GPU_SRC_MUX(sel), khz, div;

	if (!GPU_PRE_MUX_PVTPLL(sel))
		return OK;

	khz = pll_khz(gpu_src[src].index, gpu_src[src].mode_shift);
	if (khz == 0) {
		log_warn(&mali_log, "the GPU clock comes from its PVTPLL, and "
		    "the PLL path's source (%s) is not one this driver can "
		    "compute; not touching the GPU\n", gpu_src[src].name);
		return EIO;
	}
	for (div = 1; div <= 16 && khz / div > MALI_SAFE_KHZ; div++)
		;
	if (div > 16) {
		log_warn(&mali_log, "%s at %u kHz cannot be divided below %u "
		    "kHz; not touching the GPU\n", gpu_src[src].name, khz,
		    MALI_SAFE_KHZ);
		return EIO;
	}

	log_info(&mali_log, "the GPU clock comes from its PVTPLL, which this "
	    "driver cannot check; moving it to %s/%u = %u kHz\n",
	    gpu_src[src].name, div, khz / div);
	/* Bit 11 to the PLL path, the divider in 3:0, both write-masked. */
	mali_wr(cru, CRU_CLKSEL_CON(CLKSEL_GPU),
	    ((0x800u | 0xfu) << 16) | (div - 1));

	sel = mali_rd(cru, CRU_CLKSEL_CON(CLKSEL_GPU));
	if (GPU_PRE_MUX_PVTPLL(sel) || GPU_SRC_DIV(sel) != div - 1) {
		log_warn(&mali_log, "the GPU clock mux did not move "
		    "(CLKSEL_CON6 0x%08x); not touching the GPU\n", sel);
		return EIO;
	}
	return OK;
}

int
mali_rk_clocks_on(void)
{
	const struct mali_regs *cru = &mali.info.cru;
	uint32_t gate, want = GATE_CLK_GPU_SRC | GATE_CLK_GPU;
	int r;

	if ((r = clock_source()) != OK)
		return r;

	gate = mali_rd(cru, CRU_CLKGATE_CON(CLKGATE_GPU));
	if (!(gate & want))
		return OK;

	log_info(&mali_log, "opening the GPU clock gates (0x%08x)\n", gate);
	/* Write-masked: the high half says which low bits to change. */
	mali_wr(cru, CRU_CLKGATE_CON(CLKGATE_GPU), want << 16);

	/*
	 * Read back: a gate that stays shut means a block nobody may touch,
	 * and touching the GPU behind it would hang the bus.
	 */
	if ((gate = mali_rd(cru, CRU_CLKGATE_CON(CLKGATE_GPU))) & want) {
		log_warn(&mali_log, "the GPU clock gates stay shut "
		    "(0x%08x)\n", gate);
		return EIO;
	}
	return OK;
}

/*
 * The power domain: power up, then let the bus out of idle - and wait for
 * the PMU to acknowledge the request as well as for the idle status to
 * clear, which is the vendor kernel's handshake (fb and xhci wait for the
 * status alone and have not been bitten; the GPU domain is the one that
 * Linux switches off and on at run time, so the full handshake is the one
 * to copy here).
 */
int
mali_rk_power_domain(void)
{
	const struct mali_regs *pmu = &mali.info.pmu;
	unsigned spins;

	if (!(mali_rd(pmu, PMU_PWR_STATUS) & PD_GPU_PWR) &&
	    !(mali_rd(pmu, PMU_IDLE_ST) & PD_GPU_IDLE) &&
	    !(mali_rd(pmu, PMU_IDLE_ACK) & PD_GPU_IDLE)) {
		log_info(&mali_log, "power domain PD_GPU is on\n");
		return OK;
	}

	log_info(&mali_log, "power domain PD_GPU is off (status 0x%x, idle "
	    "0x%x, ack 0x%x); switching it on\n",
	    mali_rd(pmu, PMU_PWR_STATUS), mali_rd(pmu, PMU_IDLE_ST),
	    mali_rd(pmu, PMU_IDLE_ACK));

	mali_wr(pmu, PMU_PWR_CON, PD_GPU_PWR << 16);
	for (spins = 0; spins < 10000; spins++) {
		if (!(mali_rd(pmu, PMU_PWR_STATUS) & PD_GPU_PWR))
			break;
		micro_delay(10);
	}
	if (mali_rd(pmu, PMU_PWR_STATUS) & PD_GPU_PWR) {
		log_warn(&mali_log, "power domain PD_GPU did not come up\n");
		return EIO;
	}

	mali_wr(pmu, PMU_IDLE_REQ, PD_GPU_IDLE << 16);
	for (spins = 0; spins < 10000; spins++) {
		if (!(mali_rd(pmu, PMU_IDLE_ACK) & PD_GPU_IDLE) &&
		    !(mali_rd(pmu, PMU_IDLE_ST) & PD_GPU_IDLE))
			break;
		micro_delay(10);
	}
	if ((mali_rd(pmu, PMU_IDLE_ACK) & PD_GPU_IDLE) ||
	    (mali_rd(pmu, PMU_IDLE_ST) & PD_GPU_IDLE)) {
		log_warn(&mali_log, "power domain PD_GPU stayed idle (ack "
		    "0x%x, idle 0x%x)\n", mali_rd(pmu, PMU_IDLE_ACK),
		    mali_rd(pmu, PMU_IDLE_ST));
		return EIO;
	}

	log_info(&mali_log, "power domain PD_GPU is on\n");
	return OK;
}

/*
 * The reset lines.  Neither tree names them for the GPU and neither Linux
 * driver touches them - the core is reset through its own command register
 * - so this only lets go of lines somebody else left held.
 */
void
mali_rk_resets(void)
{
	const struct mali_regs *cru = &mali.info.cru;
	uint32_t rst;

	rst = mali_rd(cru, CRU_SOFTRST_CON(SOFTRST_GPU));
	if (!(rst & SRST_GPU_ALL))
		return;

	log_info(&mali_log, "releasing the GPU reset lines (0x%08x)\n", rst);
	mali_wr(cru, CRU_SOFTRST_CON(SOFTRST_GPU), SRST_GPU_ALL << 16);
	micro_delay(10);
}
