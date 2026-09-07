/*
 * The Rockchip half: everything around the Synopsys controller that the
 * Synopsys documentation does not mention.
 *
 * Three of the things here cannot be worked out from the controller, and
 * each was read out of a working system before it was written (the sources
 * and the arithmetic are in port/cb2-gmac/registers.md):
 *
 *   - which of two pin sets the controller is wired to.  One bit in the
 *     GRF.  Nothing in the tree states it directly and nothing in the
 *     controller reflects it; get it wrong and fifteen correctly
 *     multiplexed pins lead nowhere at all.
 *   - the RGMII delay lines, which live in the GRF and not in the MAC, with
 *     values that belong to the board and are in its device tree.
 *   - link speed, which on this SoC is not a register field of the
 *     controller but the rate of a clock, selected by a two-bit field of
 *     one CRU register.
 *
 * All the registers written here are hiword-masked: the write says in its
 * upper half which bits of its lower half it means.  So none of this is
 * read-modify-write, and none of it can lose a neighbouring field to a
 * race with whoever else writes the same register.
 */

#include <minix/drivers.h>
#include <sys/mman.h>

#include "dwmac.h"
#include "dwmacreg.h"
#include "dwmac_pins.h"

int
dwmac_rk_map(void)
{
	struct dwmac_devinfo *in = &dwmac.info;
	void *v;

	v = vm_map_phys(SELF, (void *)in->base, in->size);
	if (v == MAP_FAILED) {
		log_warn(&dwmac_log, "cannot map the controller at 0x%lx; "
		    "not granted by RS?\n", (unsigned long)in->base);
		return EPERM;
	}
	dwmac.mac = (vir_bytes)v;

	/*
	 * The three blocks around it.  A driver that has the controller and
	 * not the GRF can read the version register and do nothing else, so
	 * each of these is an error rather than a warning - except the GPIO,
	 * which only resets the PHY, and a PHY the bootloader left running
	 * may well answer without it.
	 */
	if (in->grf_base == 0) {
		log_warn(&dwmac_log, "the tree names no GRF for this "
		    "controller\n");
		return ENXIO;
	}
	v = vm_map_phys(SELF, (void *)in->grf_base, in->grf_size);
	if (v == MAP_FAILED) {
		log_warn(&dwmac_log, "cannot map the GRF at 0x%lx\n",
		    (unsigned long)in->grf_base);
		return EPERM;
	}
	dwmac.grf = (vir_bytes)v;

	if (in->cru_base == 0) {
		log_warn(&dwmac_log, "the tree names no CRU\n");
		return ENXIO;
	}
	v = vm_map_phys(SELF, (void *)in->cru_base, in->cru_size);
	if (v == MAP_FAILED) {
		log_warn(&dwmac_log, "cannot map the CRU at 0x%lx\n",
		    (unsigned long)in->cru_base);
		return EPERM;
	}
	dwmac.cru = (vir_bytes)v;

	if (in->gpio_base != 0 && in->reset_pin >= 0) {
		v = vm_map_phys(SELF, (void *)in->gpio_base, in->gpio_size);
		if (v == MAP_FAILED) {
			log_warn(&dwmac_log, "cannot map the GPIO bank at "
			    "0x%lx; carrying on without resetting the PHY\n",
			    (unsigned long)in->gpio_base);
			dwmac.info.reset_pin = -1;
		} else
			dwmac.gpio = (vir_bytes)v;
	}

	return OK;
}

/*
 * Let the clocks run.  A bit set in the gate register means the clock is
 * off, so switching one on is writing a zero under its mask.
 */
void
dwmac_rk_clocks_on(void)
{
	uint32_t gates = RK3568_GMAC1_GATE_ACLK | RK3568_GMAC1_GATE_PCLK |
	    RK3568_GMAC1_GATE_PTP_REF | RK3568_GMAC1_GATE_MAC1_2TOP |
	    RK3568_GMAC1_GATE_REFOUT;

	dwmac_wr(dwmac.cru, RK3568_CRU_GMAC1_CLKGATE, gates << 16);

	/*
	 * And where the transmit and receive clock comes from.  Two fields:
	 * the path is the RGMII one rather than RMII or XPCS, and the clock
	 * itself arrives from the PHY rather than being generated here -
	 * which is what "clock_in_out = input" in the tree means, and why
	 * the SoC's own output clock is never set up at all.
	 */
	dwmac_wr(dwmac.cru, RK3568_CRU_GMAC1_CLKSEL,
	    RK_HIWORD(RK3568_GMAC1_RXTX_SRC_RGMII, RK3568_GMAC1_RXTX_SRC_MASK,
	    RK3568_GMAC1_RXTX_SRC_SHIFT) |
	    RK_HIWORD(dwmac.info.clock_from_phy ? 1 : 0, 1,
	    RK3568_GMAC1_CLK_FROM_PHY_BIT));
}

/* Pull the controller's reset lines, the ones the tree named. */
void
dwmac_rk_reset_controller(void)
{
	unsigned i, reg, bit;

	for (i = 0; i < dwmac.info.nresets; i++) {
		reg = RK3568_CRU_SOFTRST_CON(dwmac.info.reset_id[i] /
		    RK3568_CRU_RESETS_PER_REG);
		bit = dwmac.info.reset_id[i] % RK3568_CRU_RESETS_PER_REG;
		if (reg + 4 > dwmac.info.cru_size)
			continue;
		dwmac_wr(dwmac.cru, reg, RK_HIWORD(1, 1, bit));
	}

	micro_delay(10);

	for (i = 0; i < dwmac.info.nresets; i++) {
		reg = RK3568_CRU_SOFTRST_CON(dwmac.info.reset_id[i] /
		    RK3568_CRU_RESETS_PER_REG);
		bit = dwmac.info.reset_id[i] % RK3568_CRU_RESETS_PER_REG;
		if (reg + 4 > dwmac.info.cru_size)
			continue;
		dwmac_wr(dwmac.cru, reg, RK_HIWORD(0, 1, bit));
	}

	micro_delay(100);
}

/*
 * Multiplex the pins, and then say which set of them is connected.
 *
 * The order matters only in that both have to happen; the second is the one
 * a reader is likely to leave out, because nothing points at it. It is one
 * bit and it decides whether any of the first fifteen writes mean anything.
 */
void
dwmac_rk_pins(void)
{
	struct dwmac_pin_write w[DWMAC_PIN_MAX_WRITES];
	unsigned i, n;

	n = dwmac_pin_writes(w, DWMAC_PIN_MAX_WRITES);
	if (n == 0) {
		log_warn(&dwmac_log, "the pin set does not fit\n");
		return;
	}

	for (i = 0; i < n; i++) {
		dwmac_wr(dwmac.grf, w[i].reg, w[i].val | (w[i].mask << 16));
		log_debug(&dwmac_log, "iomux grf+0x%03x <- %04x/%04x\n",
		    w[i].reg, w[i].val, w[i].mask);
	}

	/* And the one that connects them: zero selects the m0 set. */
	dwmac_wr(dwmac.grf, RK3568_GRF_IOFUNC_SEL0,
	    RK_HIWORD(0, 1, RK3568_GMAC1_IOMUX_SEL_BIT));
}

/*
 * The interface mode and the two delay lines.
 *
 * The delays are the board's, out of its device tree, and they are the
 * reason a RGMII link either works or silently corrupts every frame; there
 * is nothing to derive them from and nothing to check them against short of
 * running.
 */
void
dwmac_rk_rgmii(void)
{
	dwmac_wr(dwmac.grf, RK3568_GRF_GMAC1_CON0,
	    RK_HIWORD(dwmac.info.rx_delay & RK3568_GMAC_CON0_DL_MASK,
	    RK3568_GMAC_CON0_DL_MASK, RK3568_GMAC_CON0_RX_DL_SHIFT) |
	    RK_HIWORD(dwmac.info.tx_delay & RK3568_GMAC_CON0_DL_MASK,
	    RK3568_GMAC_CON0_DL_MASK, RK3568_GMAC_CON0_TX_DL_SHIFT));

	dwmac_wr(dwmac.grf, RK3568_GRF_GMAC1_CON1,
	    RK_HIWORD(RK3568_GMAC_CON1_INTF_RGMII,
	    RK3568_GMAC_CON1_INTF_SEL_MASK, RK3568_GMAC_CON1_INTF_SEL_SHIFT) |
	    RK_HIWORD(1, 1, RK3568_GMAC_CON1_RXCLK_DLY) |
	    RK_HIWORD(1, 1, RK3568_GMAC_CON1_TXCLK_DLY));
}

/*
 * The transmit delay on its own, without disturbing the receive one.
 *
 * The delay line is a pad setting, not a clock setting: it can be moved
 * while the controller runs, which is what lets the driver walk the range
 * and let the far end say which value works.  There is no way to measure
 * this from the board itself - the MAC counts a frame as sent whatever the
 * PHY makes of it - so the only instrument is a reply coming back.
 */
/*
 * The transmit clock selector, written raw.
 *
 * dwmac_rk_set_speed() picks this from the negotiated speed on the
 * assumption that clk_gmac1 is 125 MHz - which is what it is when the PHY
 * feeds the SoC 125 MHz, as "clock_in_out = input" is supposed to mean.  If
 * the PHY actually feeds something else, every one of those choices is
 * wrong by the same factor, and the way to find out is to try them: the MAC
 * will clock frames out at whatever rate it is given and count them all as
 * good, so nothing on this side of the interface can tell.
 */
void
dwmac_rk_set_speed_sel(unsigned sel)
{
	dwmac_wr(dwmac.cru, RK3568_CRU_GMAC1_CLKSEL,
	    RK_HIWORD(sel, RK3568_GMAC1_SPEED_MASK,
	    RK3568_GMAC1_SPEED_SHIFT));
}

void
dwmac_rk_set_txdelay(unsigned tx_delay)
{
	dwmac_wr(dwmac.grf, RK3568_GRF_GMAC1_CON0,
	    RK_HIWORD(dwmac.info.rx_delay & RK3568_GMAC_CON0_DL_MASK,
	    RK3568_GMAC_CON0_DL_MASK, RK3568_GMAC_CON0_RX_DL_SHIFT) |
	    RK_HIWORD(tx_delay & RK3568_GMAC_CON0_DL_MASK,
	    RK3568_GMAC_CON0_DL_MASK, RK3568_GMAC_CON0_TX_DL_SHIFT));
}

/*
 * Link speed.  On this SoC that is a clock rate and not a register bit of
 * the controller, and the rate is chosen by a two-bit field: the transmit
 * clock is divided by one, five or fifty from the 125 MHz the PHY provides.
 */
void
dwmac_rk_set_speed(unsigned speed)
{
	unsigned sel;

	switch (speed) {
	case 10:	sel = RK3568_GMAC1_SPEED_10;	break;
	case 100:	sel = RK3568_GMAC1_SPEED_100;	break;
	case 1000:	sel = RK3568_GMAC1_SPEED_1000;	break;
	default:
		log_warn(&dwmac_log, "no clock for %u Mbit/s\n", speed);
		return;
	}

	dwmac_wr(dwmac.cru, RK3568_CRU_GMAC1_CLKSEL,
	    RK_HIWORD(sel, RK3568_GMAC1_SPEED_MASK,
	    RK3568_GMAC1_SPEED_SHIFT));
}

/*
 * Reset the PHY with the line the board wired to it.
 *
 * The delays come from the tree and are long - a quarter of a second in all
 * on this board, most of it after the line is released, because a PHY needs
 * that long before it will answer on MDIO. This is the slowest thing in the
 * driver's startup by two orders of magnitude, and it happens once.
 */
void
dwmac_rk_reset_phy(void)
{
	unsigned pin, half, bit;
	unsigned dr, ddr;
	int asserted, released;

	if (dwmac.gpio == 0 || dwmac.info.reset_pin < 0)
		return;

	pin = (unsigned)dwmac.info.reset_pin;
	half = (pin >= 16);
	bit = pin % 16;
	dr = half ? RK_GPIO_SWPORT_DR_H : RK_GPIO_SWPORT_DR_L;
	ddr = half ? RK_GPIO_SWPORT_DDR_H : RK_GPIO_SWPORT_DDR_L;

	asserted = dwmac.info.reset_active_low ? 0 : 1;
	released = !asserted;

	/* An output, then held, then let go. */
	dwmac_wr(dwmac.gpio, ddr, RK_HIWORD(1, 1, bit));

	if (dwmac.info.reset_delay_us[0] != 0)
		micro_delay(dwmac.info.reset_delay_us[0]);

	dwmac_wr(dwmac.gpio, dr, RK_HIWORD(asserted, 1, bit));
	micro_delay(dwmac.info.reset_delay_us[1] != 0 ?
	    dwmac.info.reset_delay_us[1] : 10000);

	dwmac_wr(dwmac.gpio, dr, RK_HIWORD(released, 1, bit));
	micro_delay(dwmac.info.reset_delay_us[2] != 0 ?
	    dwmac.info.reset_delay_us[2] : 100000);

	log_debug(&dwmac_log, "phy reset on gpio pin %u\n", pin);
}
