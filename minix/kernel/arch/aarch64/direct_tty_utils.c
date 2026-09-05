/*
 * Output that goes straight to the console, without a driver, a message or a
 * buffer in between: what the kernel uses before there is anything to talk
 * to, and after a panic when there is no longer anything to talk to.
 */

#include "kernel/kernel.h"

#include "direct_utils.h"
#include "bsp_serial.h"

void
direct_cls(void)
{
	/*
	 * Nothing to clear. This exists for the ports whose console is a
	 * frame buffer; on a serial line the request is meaningless, and
	 * emitting an escape sequence on the chance that something is
	 * interpreting it would be worse than doing nothing.
	 */
}

void
ser_putc(char c)
{
	bsp_ser_putc(c);
}

void
direct_print_char(char c)
{
	if (c == '\n')
		bsp_ser_putc('\r');
	ser_putc(c);
}

void
direct_print(const char *str)
{
	while (*str != '\0')
		direct_print_char(*str++);
}

int
direct_read_char(unsigned char *ch)
{
	/*
	 * Nothing reads the console yet. Answering "no character" is the
	 * honest answer and the one the caller is prepared for; it asks in a
	 * loop, waiting for a key that will start arriving when the receive
	 * side of the port is wired up.
	 */
	return 0;
}
