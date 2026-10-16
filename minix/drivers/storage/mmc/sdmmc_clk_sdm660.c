/*
 * The Qualcomm SDM630/SDM660 global clock controller, as far as the SD host
 * needs it: whether a branch clock is running.  Nothing else.
 *
 * The host of that SoC is taken over as the boot loader left it
 * (msm_takeover() in sdmmc_sdhci.c) and never clocked, reset or set at a rate
 * by this driver, so enable, set_rate, set_phase and reset are absent and
 * answer ENOSYS through sdmmc_clk.c.  What is needed is the one question that
 * has to be asked before the host's first register read: on this SoC a read
 * of a block whose interface clock is gated does not come back with ones, it
 * stalls the bus, and nothing after it can report anything.
 *
 * A branch's CBCR has CLK_OFF in bit 31, set by the hardware while the branch
 * is not running (Linux, drivers/clk/qcom/clk-branch.c: CBCR_CLK_OFF).  It is
 * a status bit: reading it changes nothing.
 *
 * The numbers are the device tree's (dt-bindings/clock/qcom,gcc-sdm660.h) and
 * the CBCR offsets gcc-sdm660.c gives their halt registers - the same rows as
 * minix/drivers/hid/qcom_touch/sdm660_gcc.h, which port/clover/
 * gen-sdm660-tables.py generates from those two files; the host stand
 * (port/test/sdmmc/clktest.c) checks these against it.
 */

#include <minix/drivers.h>

#include <errno.h>
#include <stdint.h>

#include "sdmmc.h"

#define CBCR_CLK_OFF	(1u << 31)

static const struct {
	unsigned	id;
	uint32_t	cbcr;
} branches[] = {
	{ 74, 0x16008 },	/* GCC_SDCC1_AHB_CLK	 (eMMC "iface") */
	{ 75, 0x16004 },	/* GCC_SDCC1_APPS_CLK	 (eMMC "core") */
	{ 76, 0x1600c },	/* GCC_SDCC1_ICE_CORE_CLK (eMMC "ice") */
	{ 77, 0x14008 },	/* GCC_SDCC2_AHB_CLK	 (socket "iface") */
	{ 78, 0x14004 },	/* GCC_SDCC2_APPS_CLK	 (socket "core") */
};

static int
gcc_is_on(volatile uint8_t *regs, size_t size, unsigned id)
{
	unsigned i;

	for (i = 0; i < sizeof(branches) / sizeof(branches[0]); i++) {
		if (branches[i].id != id)
			continue;
		if (branches[i].cbcr + 4 > size)
			return -1;
		return (*(volatile uint32_t *)(regs + branches[i].cbcr) &
		    CBCR_CLK_OFF) ? 0 : 1;
	}
	return -1;
}

const struct sdmmc_clkctl_ops sdmmc_sdm660_gcc = {
	.enable		= NULL,
	.set_rate	= NULL,
	.set_phase	= NULL,
	.reset		= NULL,
	.is_on		= gcc_is_on,
};
