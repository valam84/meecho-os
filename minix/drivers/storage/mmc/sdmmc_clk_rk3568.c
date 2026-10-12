/*
 * The RK3566/RK3568 clock and reset unit, as far as an SD/MMC host needs it.
 *
 * Numbers, not text: the line numbers are the binding's
 * (include/dt-bindings/clock/rk3568-cru.h), the register fields are the ones
 * Linux's clk-rk3568.c names for the same clocks, and the parent rates are
 * what this board actually runs - read off the clock tree of the vendor
 * system on it (port/cb2-sd/linux-reference.txt), where the general PLL is at
 * 1188 MHz and its "400 MHz" output is really 396. That is the rate the
 * bootloader programs (U-Boot's GPLL_HZ for this SoC), and nothing on this
 * system reprograms a PLL, so the table holds as long as the bootloader does.
 * Should one ever disagree, the error is on the side of a slower card for
 * every entry that is a nominal rate rounded up, and that is why none of them
 * is rounded down.
 *
 * Every register of this unit carries its own write mask in the top half:
 * the value goes in bits 15:0 and a one in bit n+16 lets bit n change. So no
 * field is ever read back before it is written, and nothing here needs a
 * lock against another driver writing a different field of the same register
 * - which the network driver does, to CLKSEL registers of its own.
 */

#include <minix/drivers.h>
#include <minix/log.h>

#include <errno.h>
#include <stdint.h>

#include "sdmmc.h"

#define RK_CLKSEL_CON(n)	(0x100 + 4 * (n))
#define RK_CLKGATE_CON(n)	(0x300 + 4 * (n))
#define RK_SOFTRST_CON0		0x400
#define RK_SOFTRST_PER_REG	16

/* Value v in the field at shift, width bits, with its write mask. */
#define RK_HIWORD(v, width, shift) \
	((((uint32_t)1 << (width)) - 1) << ((shift) + 16) | \
	 ((uint32_t)(v) << (shift)))

/*
 * The multiplexer inputs, in register order.
 *
 * cclk_emmc: xin24m, gpll_200m, gpll_150m, cpll_100m, cpll_50m and the
 * oscillator divided to 375 kHz - the one input under a megahertz, and so
 * the one the eMMC is identified on (see sdmmc.h, SDMMC_IDENT_HZ).
 *
 * clk_sdmmc0..2: xin24m, gpll_400m, gpll_300m, cpll_100m, cpll_50m and the
 * oscillator divided to 750 kHz. The DesignWare controller halves its input
 * before its own divider, so 750 kHz is the 375 kHz identification clock of
 * the SD socket.
 */
static const uint32_t cclk_emmc_parents[] = {
	24000000, 198000000, 148500000, 100000000, 50000000, 375000
};
static const uint32_t clk_sdmmc_parents[] = {
	24000000, 396000000, 297000000, 100000000, 50000000, 750000
};

#define NPARENTS(a)	(sizeof(a) / sizeof((a)[0]))

/*
 * One clock: its gate, and its multiplexer when it has one. A clock with no
 * multiplexer is a bus clock that only goes on and off.
 */
struct rk_clk {
	unsigned	id;
	unsigned	gate_reg, gate_bit;
	unsigned	mux_reg, mux_shift, mux_width;	/* mux_reg 0: none */
	const uint32_t *parents;
	unsigned	nparents;
};

static const struct rk_clk clks[] = {
	/* the eMMC controller: ACLK, HCLK, BCLK, CCLK, TCLK_EMMC */
	{ 121, RK_CLKGATE_CON(9), 5,  0, 0, 0, NULL, 0 },
	{ 122, RK_CLKGATE_CON(9), 6,  0, 0, 0, NULL, 0 },
	{ 123, RK_CLKGATE_CON(9), 7,  0, 0, 0, NULL, 0 },
	{ 124, RK_CLKGATE_CON(9), 8,  RK_CLKSEL_CON(28), 12, 3,
	    cclk_emmc_parents, NPARENTS(cclk_emmc_parents) },
	{ 125, RK_CLKGATE_CON(9), 9,  0, 0, 0, NULL, 0 },
	/* SD/SDIO controller 0: HCLK_SDMMC0, CLK_SDMMC0 */
	{ 176, RK_CLKGATE_CON(15), 0, 0, 0, 0, NULL, 0 },
	{ 177, RK_CLKGATE_CON(15), 1, RK_CLKSEL_CON(30), 8, 3,
	    clk_sdmmc_parents, NPARENTS(clk_sdmmc_parents) },
	/* controller 1 */
	{ 178, RK_CLKGATE_CON(15), 2, 0, 0, 0, NULL, 0 },
	{ 179, RK_CLKGATE_CON(15), 3, RK_CLKSEL_CON(30), 12, 3,
	    clk_sdmmc_parents, NPARENTS(clk_sdmmc_parents) },
	/* controller 2 */
	{ 193, RK_CLKGATE_CON(17), 0, 0, 0, 0, NULL, 0 },
	{ 194, RK_CLKGATE_CON(17), 1, RK_CLKSEL_CON(32), 8, 3,
	    clk_sdmmc_parents, NPARENTS(clk_sdmmc_parents) },
};

/*
 * The phase clocks: SCLK_SDMMC0_DRV (394) through SCLK_EMMC_SAMPLE (401),
 * a drive and a sample register per controller, in that order from 0x580.
 *
 * The field is eleven bits at bit 1: the quarter-turn in the low two, then a
 * count of fine delay elements and the bit that switches them in. Only the
 * quarter-turns are used here. They are exact on every Rockchip part; the
 * fine elements are about 60 ps each and vary with the silicon, and the
 * modes this driver runs - up to 50 MHz on a socket - are the ones Linux
 * drives with whole quarter-turns too.
 *
 * Checked against the board: under Linux at SDR104 the drive register reads
 * 0x004 (two quarter-turns, 180 degrees, as Linux reports) and the sample
 * register 0x858 (no quarter-turn, eleven fine elements, delay on: the
 * tuned 35 degrees).
 */
#define RK_MMC_PHASE_FIRST	394
#define RK_MMC_PHASE_LAST	401
#define RK_MMC_PHASE_REG0	0x580
#define RK_MMC_PHASE_SHIFT	1
#define RK_MMC_PHASE_WIDTH	11

static const struct rk_clk *
find_clk(unsigned id)
{
	unsigned i;

	for (i = 0; i < sizeof(clks) / sizeof(clks[0]); i++)
		if (clks[i].id == id)
			return &clks[i];
	return NULL;
}

static int
wr(volatile uint8_t *regs, size_t size, unsigned off, uint32_t v)
{
	if (off + 4 > size)
		return EINVAL;
	*(volatile uint32_t *)(regs + off) = v;
	return OK;
}

static int
rk_enable(volatile uint8_t *regs, size_t size, unsigned id)
{
	const struct rk_clk *c = find_clk(id);

	if (c == NULL)
		return ENOSYS;
	/* A gate bit set is the clock stopped; clearing it starts it. */
	return wr(regs, size, c->gate_reg, RK_HIWORD(0, 1, c->gate_bit));
}

/*
 * The input with the highest rate at or below hz, or the lowest input when
 * every one is above it - and in that case the caller divides.
 */
static int
rk_set_rate(volatile uint8_t *regs, size_t size, unsigned id, uint32_t hz,
	uint32_t *got)
{
	const struct rk_clk *c = find_clk(id);
	unsigned i, best = 0, lowest = 0;
	uint32_t best_hz = 0;
	int r;

	if (c == NULL || c->mux_reg == 0 || hz == 0)
		return ENOSYS;

	for (i = 0; i < c->nparents; i++) {
		if (c->parents[i] <= hz && c->parents[i] > best_hz) {
			best = i;
			best_hz = c->parents[i];
		}
		if (c->parents[i] < c->parents[lowest])
			lowest = i;
	}
	if (best_hz == 0)
		best = lowest;

	if ((r = wr(regs, size, c->mux_reg,
	    RK_HIWORD(best, c->mux_width, c->mux_shift))) != OK)
		return r;
	(void)rk_enable(regs, size, id);

	log_debug(&sdmmc_log, "clock %u: input %u (%u Hz) for %u Hz\n", id,
	    best, c->parents[best], hz);
	if (got != NULL)
		*got = c->parents[best];
	return OK;
}

static int
rk_set_phase(volatile uint8_t *regs, size_t size, unsigned id,
	unsigned degrees)
{
	unsigned reg;

	if (id < RK_MMC_PHASE_FIRST || id > RK_MMC_PHASE_LAST)
		return ENOSYS;
	if (degrees % 90 != 0)
		return EINVAL;	/* only whole quarter-turns; see above */

	reg = RK_MMC_PHASE_REG0 + 4 * (id - RK_MMC_PHASE_FIRST);
	log_debug(&sdmmc_log, "clock %u: phase %u degrees\n", id, degrees);
	return wr(regs, size, reg, RK_HIWORD(degrees / 90, RK_MMC_PHASE_WIDTH,
	    RK_MMC_PHASE_SHIFT));
}

/*
 * The software reset lines: sixteen to a register from 0x400, line n in
 * register n / 16 at bit n % 16, and a set bit holds the block in reset.
 */
static int
rk_reset(volatile uint8_t *regs, size_t size, unsigned id, int asserted)
{
	unsigned reg = RK_SOFTRST_CON0 + (id / RK_SOFTRST_PER_REG) * 4;

	return wr(regs, size, reg,
	    RK_HIWORD(asserted ? 1 : 0, 1, id % RK_SOFTRST_PER_REG));
}

const struct sdmmc_clkctl_ops sdmmc_rk3568_cru = {
	.enable		= rk_enable,
	.set_rate	= rk_set_rate,
	.set_phase	= rk_set_phase,
	.reset		= rk_reset,
};
