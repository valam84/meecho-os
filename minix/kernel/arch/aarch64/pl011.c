/*
 * The PL011 half of the kernel console.
 *
 * Moved here out of bsp/qemu-virt/virt_serial.c when the CB2 turned out to
 * have a DesignWare 8250 instead: which UART a machine has is a property of
 * the machine, reported by its device tree, exactly as the GIC version is.
 * The base address arrives as an argument because serial.c owns it - it is
 * the variable VM rewrites when the range gets mapped.
 */

#include <sys/types.h>
#include <minix/type.h>
#include <io.h>

#include "kernel/kernel.h"

#include "serial.h"

void
pl011_init(vir_bytes base)
{
	/*
	 * Bring the UART down before touching its configuration, and wait for
	 * any character still on the wire, as the PL011 manual requires.
	 */
	mmio_write(base + PL011_CR, 0);
	while (mmio_read(base + PL011_FR) & PL011_FR_BUSY)
		;

	/* Clearing FEN flushes the FIFOs. */
	mmio_write(base + PL011_LCRH, 0);

	mmio_write(base + PL011_IMSC, 0);
	mmio_write(base + PL011_ICR, PL011_INT_ALL);

	/*
	 * QEMU ignores the baud rate divisors entirely, so there is nothing
	 * useful to program here. On the BCM2711 they matter and depend on the
	 * UART reference clock the firmware chose, which is why config.txt has
	 * to pin init_uart_clock.
	 */

	mmio_write(base + PL011_LCRH, PL011_LCRH_WLEN_8 | PL011_LCRH_FEN);
	mmio_write(base + PL011_CR,
	    PL011_CR_UARTEN | PL011_CR_TXE | PL011_CR_RXE);
}

void
pl011_putc(vir_bytes base, char c)
{
	/* Wait for room in the transmit FIFO. */
	while (mmio_read(base + PL011_FR) & PL011_FR_TXFF)
		;

	mmio_write(base + PL011_DR, (u32_t)(unsigned char)c);

	/*
	 * Drain before returning. This makes the console slow, but it is the
	 * only behaviour that reliably survives a panic, which is exactly when
	 * the output matters.
	 */
	while (mmio_read(base + PL011_FR) & PL011_FR_BUSY)
		;
}
