/*
 * Which pins the controller needs, and what that turns into.
 *
 * Separated from the rest of the glue because it is the one part of this
 * driver that is pure arithmetic, and the one part where a mistake is
 * silent: a wrong register or a wrong nibble multiplexes some other pin,
 * and the only symptom is a link that never comes up.  Here it has no
 * dependency on anything MINIX, so port/test/dwmac can compile it on a host
 * and check the six writes it produces against the table worked out by hand
 * in port/cb2-gmac/registers.md - two derivations of the same numbers, from
 * different directions.
 *
 * The pin list is the one the board's device tree asks for by name
 * (gmac1m0-miim, -tx-bus2, -rx-bus2, -rgmii-bus, -rgmii-clk, -clkinout):
 * bank 3 throughout, function 3 throughout.
 */

#include <stdint.h>
#include <string.h>

#include "dwmacreg.h"
#include "dwmac_pins.h"

static const uint8_t gmac1m0_pins[] = {
	2, 3,				/* rgmii bus, the pair with drive */
	4, 5,				/* rgmii bus */
	6, 7,				/* the two clocks */
	9, 10, 11,			/* rx bus */
	13, 14, 15,			/* tx bus */
	16,				/* clkinout: the clock the PHY feeds */
	20, 21				/* MDC and MDIO */
};

/*
 * A bank has four multiplexer entries, one per eight pins, and each entry
 * spans two registers: a hiword-masked register carries only sixteen bits
 * of data, which is four pins of four bits.
 */
unsigned
dwmac_pin_iomux_reg(unsigned bank, unsigned pin)
{
	unsigned reg;

	reg = RK3568_GRF_IOMUX_GPIO3 +
	    (bank - DWMAC_PIN_BANK) * RK3568_GRF_IOMUX_BANK_STRIDE;
	reg += (pin / 8) * 8;
	if ((pin % 8) >= 4)
		reg += 4;

	return reg;
}

unsigned
dwmac_pin_iomux_shift(unsigned pin)
{
	return (pin % 4) * 4;
}

/*
 * The writes that multiplex every pin of the set, one per register touched,
 * in increasing order of register.  Returns how many there are, or zero if
 * they would not fit - which cannot happen for the list above and is
 * checked anyway, because the day someone adds a pin is the day it can.
 */
unsigned
dwmac_pin_writes(struct dwmac_pin_write *out, unsigned max)
{
	unsigned i, reg, shift, slot, n = 0;

	memset(out, 0, sizeof(*out) * max);

	for (i = 0; i < sizeof(gmac1m0_pins) / sizeof(gmac1m0_pins[0]); i++) {
		reg = dwmac_pin_iomux_reg(DWMAC_PIN_BANK, gmac1m0_pins[i]);
		shift = dwmac_pin_iomux_shift(gmac1m0_pins[i]);

		for (slot = 0; slot < n; slot++)
			if (out[slot].reg == reg)
				break;
		if (slot == n) {
			if (n == max)
				return 0;
			out[n++].reg = reg;
		}

		out[slot].val |= (uint32_t)DWMAC_PIN_FUNC << shift;
		out[slot].mask |= 0xfu << shift;
	}

	return n;
}
