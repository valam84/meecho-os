#ifndef _DWMAC_PINS_H
#define _DWMAC_PINS_H

#include <stdint.h>

/* The pin set of GMAC1 on this SoC: bank 3, function 3, all of it. */
#define DWMAC_PIN_BANK		3
#define DWMAC_PIN_FUNC		3

/* No bank needs more than four registers; six is room to be wrong in. */
#define DWMAC_PIN_MAX_WRITES	6

struct dwmac_pin_write {
	unsigned reg;			/* offset in the GRF */
	uint32_t val;			/* the four-bit fields, in place */
	uint32_t mask;			/* which of them this write means */
};

unsigned dwmac_pin_iomux_reg(unsigned bank, unsigned pin);
unsigned dwmac_pin_iomux_shift(unsigned pin);
unsigned dwmac_pin_writes(struct dwmac_pin_write *out, unsigned max);

#endif /* _DWMAC_PINS_H */
