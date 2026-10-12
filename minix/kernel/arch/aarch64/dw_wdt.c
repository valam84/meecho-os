/*
 * The DesignWare APB watchdog ("snps,dw-wdt"), the part behind hwwd.c.
 *
 * Registers, as read from the CB2's RK3566 under its vendor Linux
 * (port/cb2-wdt/reference.txt): component version 1.10a, a 32-bit counter
 * (COMP_PARAMS_1 CNT_WIDTH 16+16), fixed timeout ranges (USE_FIX_TOP), no
 * "always enabled" strap, found disabled with CR = 0x08 (reset pulse length
 * 2, i.e. 8 pclk cycles). Fed with 24 MHz on tclk.
 *
 * Three properties of the part decide how this file is written:
 *
 *  - Once enabled it cannot be disabled: WDT_EN clears only on reset. So
 *    nothing here runs unless the command line asked for it, and the
 *    decision is final until the machine resets.
 *  - The timeout is not a number of seconds but a range: TOP selects
 *    2^(16+TOP) cycles of tclk. Which seconds that is depends on the clock,
 *    and the clock rate is not in the device tree - "clocks" names CRU
 *    outputs by phandle, and the kernel has no clock framework to ask. So
 *    the rate is measured: the counter runs from enable onwards, and the
 *    generic timer, whose rate CNTFRQ_EL0 gives, is the ruler.
 *  - The response mode is "reset" (RMOD 0): at timeout the part pulls the
 *    system reset. The other mode raises an interrupt first and resets on
 *    the second timeout; an interrupt is no use when the failure being
 *    caught is a kernel that does not take interrupts.
 */

#include <sys/types.h>
#include <minix/type.h>
#include <io.h>

#include "kernel/kernel.h"

#include "hwwd.h"

#define WDT_CR			0x00	/* control */
#define   WDT_CR_EN		  0x01	/* enable; cleared only by reset */
#define   WDT_CR_RMOD		  0x02	/* 1: interrupt first, then reset */
#define WDT_TORR		0x04	/* timeout range: TOP 3:0, TOP_INIT 7:4 */
#define WDT_CCVR		0x08	/* current counter value */
#define WDT_CRR			0x0c	/* counter restart */
#define   WDT_CRR_KICK		  0x76	/* the only value that restarts */
#define WDT_COMP_PARAMS_1	0xf4
#define   WDT_P1_USE_FIX_TOP	  0x40
#define   WDT_P1_CNT_WIDTH(p)	  (((p) >> 24) & 0x1f)

/* How long to watch the counter to learn its rate: 20 ms. */
#define MEASURE_DIV		50

static u64_t
cntvct(void)
{
	u64_t v;

	__asm__ volatile("isb; mrs %0, cntvct_el0" : "=r"(v));
	return v;
}

/*===========================================================================*
 *				dw_wdt_pet				     *
 *===========================================================================*/
void
dw_wdt_pet(vir_bytes base)
{
	mmio_write(base + WDT_CRR, WDT_CRR_KICK);
}

/*===========================================================================*
 *				dw_wdt_start				     *
 *===========================================================================*/
/*
 * Enable the watchdog in reset mode with the shortest range that is at least
 * 'seconds'. Reports the measured tclk rate and the timeout that was set.
 * Returns 0, or -1 (and leaves the part untouched) for a configuration this
 * code does not understand.
 */
int
dw_wdt_start(vir_bytes base, unsigned seconds, u64_t cntfrq,
	u32_t *rate_out, u32_t *ms_out)
{
	u32_t params, cr, c0, c1;
	u64_t t0, t1, rate, want;
	unsigned width, top, max_top;

	params = mmio_read(base + WDT_COMP_PARAMS_1);
	width = 16 + WDT_P1_CNT_WIDTH(params);

	/*
	 * With USE_FIX_TOP clear the ranges are user-defined values baked
	 * into the part, and their encoding is not 2^(16+TOP). Nothing met
	 * so far has that; refuse rather than guess, since a wrong guess is
	 * either a reset loop or no watchdog at all.
	 */
	if (!(params & WDT_P1_USE_FIX_TOP) || width < 16 || cntfrq == 0)
		return -1;
	max_top = width - 16;
	if (max_top > 15)
		max_top = 15;

	/*
	 * Start with the longest range - 89 s at 24 MHz - so that measuring
	 * the clock cannot itself time out. TOP_INIT (the range of the first
	 * period) set to the same value; on a part without DUAL_TOP the field
	 * is ignored.
	 */
	mmio_write(base + WDT_TORR, max_top | (max_top << 4));
	cr = mmio_read(base + WDT_CR);
	cr &= ~WDT_CR_RMOD;
	mmio_write(base + WDT_CR, cr | WDT_CR_EN);
	dw_wdt_pet(base);

	/* The rate: how far the counter falls in a known slice of time. */
	c0 = mmio_read(base + WDT_CCVR);
	t0 = cntvct();
	while (cntvct() - t0 < cntfrq / MEASURE_DIV)
		;
	c1 = mmio_read(base + WDT_CCVR);
	t1 = cntvct();
	dw_wdt_pet(base);

	rate = (c0 > c1) ? (u64_t)(c0 - c1) * cntfrq / (t1 - t0) : 0;

	/*
	 * A rate outside 1 kHz..1 GHz is a counter that did not run, or one
	 * read across a domain crossing that went wrong. Keep the longest
	 * range then: a watchdog that resets late is still a watchdog, one
	 * that resets during boot is a machine that never comes up.
	 */
	top = max_top;
	if (rate >= 1000 && rate <= 1000000000) {
		want = (u64_t)seconds * rate;
		for (top = 0; top < max_top; top++)
			if ((1ULL << (16 + top)) >= want)
				break;
	} else
		rate = 0;

	mmio_write(base + WDT_TORR, top | (top << 4));
	dw_wdt_pet(base);	/* a new range takes effect at the next restart */

	*rate_out = (u32_t)rate;
	*ms_out = rate ? (u32_t)((1ULL << (16 + top)) * 1000 / rate) : 0;
	return 0;
}
