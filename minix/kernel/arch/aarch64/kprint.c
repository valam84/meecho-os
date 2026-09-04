/*
 * Minimal console output for early boot. See kprint.h for why it is this
 * small.
 *
 * Everything here goes through bsp_ser_putc(), so it follows the console
 * wherever the BSP points it - physical registers before the MMU is on, the
 * kernel mapping afterwards.
 */

#include <stdint.h>

#include "bsp_serial.h"
#include "kprint.h"

void
kputs(const char *s)
{
	while (*s != '\0') {
		if (*s == '\n')
			bsp_ser_putc('\r');
		bsp_ser_putc(*s++);
	}
}

void
kput_hexn(uint64_t value, unsigned digits)
{
	static const char digit[] = "0123456789abcdef";
	int shift;

	kputs("0x");
	for (shift = (int)(digits - 1) * 4; shift >= 0; shift -= 4)
		bsp_ser_putc(digit[(value >> shift) & 0xf]);
}

void
kput_hex(uint64_t value)
{
	kput_hexn(value, 16);
}

void
kput_dec(uint64_t value)
{
	char buf[20];
	unsigned n = 0;

	if (value == 0) {
		bsp_ser_putc('0');
		return;
	}

	while (value != 0) {
		buf[n++] = (char)('0' + (value % 10));
		value /= 10;
	}

	while (n != 0)
		bsp_ser_putc(buf[--n]);
}

void
kput_line(const char *label, uint64_t value)
{
	kputs(label);
	kput_hex(value);
	kputs("\n");
}
