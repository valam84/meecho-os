/*
 * The Allwinner H616/H313 clock control unit, as far as an SD/MMC host needs
 * it: the three module clocks, their bus gates and their resets.
 *
 * Numbers, not text: the line numbers are the binding's
 * (include/dt-bindings/clock/sun50i-h616-ccu.h and reset/...), the register
 * layout is the one Linux's ccu-sun50i-h616.c describes for the same clocks.
 * The H313 is the H616 with fewer things wired out, and uses the same unit.
 *
 * A module clock is a multiplexer, a divider by 2^N (N two bits), a divider
 * by M (four bits, M+1) and a fixed divider by two after them:
 *
 *	rate = parent / (M + 1) / 2^N / 2
 *
 * Inputs: the 24 MHz oscillator, and PLL_PERIPH0 and PLL_PERIPH1 at twice
 * their rate. The PLLs are programmed by the bootloader, and the nominal
 * 1200 MHz of PERIPH0(2X) is what U-Boot sets on this family; PERIPH1 is not
 * offered here, because nothing guarantees it is running at all.
 *
 * Unlike Rockchip's unit, these registers carry no write mask: a field is
 * changed by reading the register, changing the field and writing it back.
 * A gate bit set is the clock running, and a reset bit set is the block *out*
 * of reset - both the other way round from the RK3568.
 *
 * Not tested on hardware: no H313 board here. The arithmetic and the words
 * written are checked by port/test/sdmmc/clktest.c on the Tanix TX1 tree.
 */

#include <minix/drivers.h>
#include <minix/log.h>

#include <errno.h>
#include <stdint.h>

#include "sdmmc.h"

/* The binding's numbers. */
#define CLK_MMC0	60		/* .. CLK_MMC2 62: module clocks */
#define CLK_BUS_MMC0	63		/* .. CLK_BUS_MMC2 65: bus gates */
#define RST_BUS_MMC0	14		/* .. RST_BUS_MMC2 16 */

#define MMC_CLK_REG(i)		(0x830 + 4 * (i))
#define  MMC_CLK_GATE		(1u << 31)
#define  MMC_CLK_MUX_SHIFT	24
#define  MMC_CLK_MUX_MASK	0x3
#define  MMC_CLK_N_SHIFT	8
#define  MMC_CLK_N_MASK		0x3
#define  MMC_CLK_M_MASK		0xf
#define MMC_BGR_REG		0x84c	/* gates in 2:0, resets in 18:16 */
#define  MMC_BGR_RST_SHIFT	16

static const uint32_t parents[] = {
	24000000,			/* osc24M */
	1200000000,			/* pll-periph0-2x */
};

static uint32_t
rd(volatile uint8_t *regs, unsigned off)
{
	return *(volatile uint32_t *)(regs + off);
}

static void
wr(volatile uint8_t *regs, unsigned off, uint32_t v)
{
	*(volatile uint32_t *)(regs + off) = v;
}

static int
in_range(size_t size, unsigned off)
{
	return off + 4 <= size;
}

static int
h616_enable(volatile uint8_t *regs, size_t size, unsigned id)
{
	unsigned i;

	if (id >= CLK_BUS_MMC0 && id <= CLK_BUS_MMC0 + 2) {
		i = id - CLK_BUS_MMC0;
		if (!in_range(size, MMC_BGR_REG))
			return EINVAL;
		wr(regs, MMC_BGR_REG, rd(regs, MMC_BGR_REG) | (1u << i));
		return OK;
	}
	if (id >= CLK_MMC0 && id <= CLK_MMC0 + 2) {
		i = id - CLK_MMC0;
		if (!in_range(size, MMC_CLK_REG(i)))
			return EINVAL;
		wr(regs, MMC_CLK_REG(i), rd(regs, MMC_CLK_REG(i)) |
		    MMC_CLK_GATE);
		return OK;
	}
	return ENOSYS;
}

/*
 * The highest rate at or below hz any input, 2^N and M can make; the lowest
 * there is when none is that low.
 */
static int
h616_set_rate(volatile uint8_t *regs, size_t size, unsigned id, uint32_t hz,
	uint32_t *got)
{
	unsigned i, p, n, m, bp = 0, bn = 0, bm = 0;
	uint32_t best = 0, lowest = ~0u, lp = 0, ln = 0, lm = 0, rate, v;

	if (id < CLK_MMC0 || id > CLK_MMC0 + 2 || hz == 0)
		return ENOSYS;
	i = id - CLK_MMC0;
	if (!in_range(size, MMC_CLK_REG(i)))
		return EINVAL;

	for (p = 0; p < sizeof(parents) / sizeof(parents[0]); p++) {
		for (n = 0; n <= MMC_CLK_N_MASK; n++) {
			for (m = 0; m <= MMC_CLK_M_MASK; m++) {
				rate = parents[p] / (m + 1) / (1u << n) / 2;
				if (rate <= hz && rate > best) {
					best = rate;
					bp = p; bn = n; bm = m;
				}
				if (rate < lowest) {
					lowest = rate;
					lp = p; ln = n; lm = m;
				}
			}
		}
	}
	if (best == 0) {
		best = lowest;
		bp = lp; bn = ln; bm = lm;
	}

	/*
	 * The gate off while the dividers change, as the manual asks of
	 * every module clock of this family, and on again afterwards.
	 */
	v = rd(regs, MMC_CLK_REG(i));
	wr(regs, MMC_CLK_REG(i), v & ~MMC_CLK_GATE);
	v &= ~((MMC_CLK_MUX_MASK << MMC_CLK_MUX_SHIFT) |
	    (MMC_CLK_N_MASK << MMC_CLK_N_SHIFT) | MMC_CLK_M_MASK);
	v |= (bp << MMC_CLK_MUX_SHIFT) | (bn << MMC_CLK_N_SHIFT) | bm;
	wr(regs, MMC_CLK_REG(i), v & ~MMC_CLK_GATE);
	wr(regs, MMC_CLK_REG(i), v | MMC_CLK_GATE);

	log_debug(&sdmmc_log, "clock %u: input %u, N %u, M %u: %u Hz for "
	    "%u Hz\n", id, bp, bn, bm + 1, best, hz);
	if (got != NULL)
		*got = best;
	return OK;
}

static int
h616_reset(volatile uint8_t *regs, size_t size, unsigned id, int asserted)
{
	uint32_t bit, v;

	if (id < RST_BUS_MMC0 || id > RST_BUS_MMC0 + 2)
		return ENOSYS;
	if (!in_range(size, MMC_BGR_REG))
		return EINVAL;
	bit = 1u << (MMC_BGR_RST_SHIFT + id - RST_BUS_MMC0);
	v = rd(regs, MMC_BGR_REG);
	wr(regs, MMC_BGR_REG, asserted ? (v & ~bit) : (v | bit));
	return OK;
}

const struct sdmmc_clkctl_ops sdmmc_h616_ccu = {
	.enable		= h616_enable,
	.set_rate	= h616_set_rate,
	.set_phase	= NULL,		/* new timing mode: no phase clocks */
	.reset		= h616_reset,
};
