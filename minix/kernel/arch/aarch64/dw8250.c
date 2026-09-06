/*
 * The 8250 half of the kernel console: the DesignWare UART of the RK3566,
 * which is what the BIGTREETECH CB2 puts its console on (uart2, 0xfe660000).
 *
 * Two things make this shorter than the PL011 driver rather than longer.
 *
 * The line is not programmed at all. The boot loader has already configured
 * this port - it is the port U-Boot itself printed on, at the rate the device
 * tree's stdout-path names (1500000 on the CB2) - and reprogramming it would
 * mean knowing the reference clock, which is a board fact the kernel has no
 * business holding a table of. Writing the divisor also means setting DLAB,
 * which swaps two registers out from under anything that prints in between;
 * a console that is already working is not worth that risk. If a machine ever
 * hands us an unconfigured port, this is where the divisor would go, and the
 * clock would have to come out of the device tree with it.
 *
 * The register spacing is not a constant. "Register n" of an 8250 is a
 * byte-wide register in the original part, but the DesignWare version sits on
 * a 32-bit bus with the registers spread four bytes apart. The device tree
 * says which through reg-shift and reg-io-width - 2 and 4 on the RK3566 - so
 * both come in as arguments rather than as #defines.
 *
 * The width matters for correctness, not only for tidiness: on a narrow
 * mapping a 32-bit read covers four consecutive registers at once, and
 * register 0 of an 8250 is the receive buffer, whose read pops a character
 * off the FIFO. Reading the status register the wrong width would quietly
 * eat input.
 */

#include <sys/types.h>
#include <minix/type.h>
#include <io.h>

#include "kernel/kernel.h"

#include "serial.h"

static u32_t
reg_read(vir_bytes base, unsigned reg, unsigned shift, unsigned width)
{
	vir_bytes a = base + ((vir_bytes)reg << shift);

	if (width == 4)
		return mmio_read(a);
	return (u32_t)*(volatile u8_t *)a;
}

static void
reg_write(vir_bytes base, unsigned reg, unsigned shift, unsigned width,
	u32_t v)
{
	vir_bytes a = base + ((vir_bytes)reg << shift);

	if (width == 4)
		mmio_write(a, v);
	else
		*(volatile u8_t *)a = (u8_t)v;
}

void
ns8250_init(vir_bytes base, unsigned shift, unsigned width)
{
	/*
	 * Deliberately empty of configuration: see the file comment. Draining
	 * whatever the loader left in flight is the one useful thing to do,
	 * and it costs nothing.
	 */
	while (!(reg_read(base, NS8250_LSR, shift, width) & NS8250_LSR_TEMT))
		;
}

void
ns8250_putc(vir_bytes base, unsigned shift, unsigned width, char c)
{
	/* Wait for room in the transmit holding register. */
	while (!(reg_read(base, NS8250_LSR, shift, width) & NS8250_LSR_THRE))
		;

	reg_write(base, NS8250_THR, shift, width, (u32_t)(unsigned char)c);

	/*
	 * Drain at the end of a line, not after every character.
	 *
	 * The output that matters most is a panic's, and a character still
	 * sitting in a FIFO when the machine stops is a character nobody
	 * sees - which argues for draining always, as the PL011 driver does.
	 * But draining per character halves an already slow console, and this
	 * is the port where the console is the only instrument there is: a
	 * kernel-side IPC dump is over a megabyte, and at that rate it takes
	 * minutes during which the system barely runs, which changes the
	 * timing of the very thing being diagnosed.
	 *
	 * Draining per line keeps the guarantee where it counts - a panic
	 * ends its line - and loses at most one line's worth if the machine
	 * stops mid-sentence.
	 */
	if (c == '\n')
		while (!(reg_read(base, NS8250_LSR, shift, width) &
		    NS8250_LSR_TEMT))
			;
}
