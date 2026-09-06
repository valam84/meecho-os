/*
 * The PL011 half of the tty serial driver: QEMU's virt machine, and the
 * BCM2711.
 *
 * This was rs232.c until the CB2 arrived with a DesignWare 8250 on its
 * console. Everything above the register level - the buffers, the ostate
 * bits, the interrupt hook, what tty.c is handed - stayed in rs232.c; what
 * came here is the dozen operations that are actually about this part. The
 * kernel's console driver was split along the same line and for the same
 * reason; see minix/kernel/arch/aarch64/serial.c.
 */

#include <minix/drivers.h>
#include <minix/fdt.h>
#include <sys/termios.h>

#include "tty.h"
#include "uart.h"

/* Registers, by byte offset. */
#define PL011_DR	0x000	/* data */
#define PL011_FR	0x018	/* flag */
#define PL011_IBRD	0x024	/* integer baud rate divisor */
#define PL011_FBRD	0x028	/* fractional baud rate divisor */
#define PL011_LCRH	0x02c	/* line control */
#define PL011_CR	0x030	/* control */
#define PL011_IFLS	0x034	/* interrupt FIFO level select */
#define PL011_IMSC	0x038	/* interrupt mask set/clear */
#define PL011_MIS	0x040	/* masked interrupt status */
#define PL011_ICR	0x044	/* interrupt clear */

#define FR_BUSY		(1 << 3)
#define FR_RXFE		(1 << 4)	/* receive FIFO empty */
#define FR_TXFF		(1 << 5)	/* transmit FIFO full */

#define LCRH_BRK	(1 << 0)	/* send break */
#define LCRH_FEN	(1 << 4)	/* enable FIFOs */
#define LCRH_WLEN_8	(3 << 5)

#define CR_UARTEN	(1 << 0)
#define CR_TXE		(1 << 8)
#define CR_RXE		(1 << 9)

#define INT_RX		(1 << 4)	/* receive */
#define INT_TX		(1 << 5)	/* transmit */
#define INT_RT		(1 << 6)	/* receive timeout */
#define INT_ALL		0x7ff

/* One quarter full either way: an interrupt while there is still room. */
#define IFLS_RX_QUARTER	(0 << 3)
#define IFLS_TX_QUARTER	(0 << 0)

#define PL011_MIN_SIZE	0x48

static unsigned int
reg_read(struct uart *u, int offset)
{
	return *(volatile unsigned int *)(u->regs + offset);
}

static void
reg_write(struct uart *u, int offset, unsigned int val)
{
	*(volatile unsigned int *)(u->regs + offset) = val;
}

static int
pl011_probe(const struct fdt_node *node, struct uart *u)
{
	u64_t base, size;

	if (fdt_node_reg(node, 0, &base, &size) != 0)
		return ENXIO;
	if (size < PL011_MIN_SIZE)
		return ENXIO;

	u->phys = (phys_bytes)base;
	u->size = (size_t)size;

	return OK;
}

static void
pl011_config(struct uart *u, int up)
{
	unsigned int cr;

	/* Quiesce the port before touching its configuration. */
	cr = reg_read(u, PL011_CR);
	reg_write(u, PL011_CR, 0);
	while (reg_read(u, PL011_FR) & FR_BUSY)
		;

	/*
	 * The baud rate divisors are deliberately left alone; rs232.c says
	 * why. QEMU ignores them entirely, and on a real board they depend
	 * on the UART reference clock.
	 */
	reg_write(u, PL011_LCRH, LCRH_WLEN_8 | LCRH_FEN);
	reg_write(u, PL011_IFLS, IFLS_RX_QUARTER | IFLS_TX_QUARTER);

	if (!up)
		return;

	reg_write(u, PL011_CR, cr | CR_UARTEN | CR_TXE | CR_RXE);
}

static void
pl011_init(struct uart *u)
{
	/* Clear anything stale, then take receive and receive-timeout. */
	reg_write(u, PL011_ICR, INT_ALL);
	u->imask = INT_RX | INT_RT;
	reg_write(u, PL011_IMSC, u->imask);

	pl011_config(u, TRUE);
}

static int
pl011_txready(struct uart *u)
{
	return !(reg_read(u, PL011_FR) & FR_TXFF);
}

static void
pl011_putc(struct uart *u, int c)
{
	reg_write(u, PL011_DR, (unsigned char)c);
}

static int
pl011_rxready(struct uart *u)
{
	return !(reg_read(u, PL011_FR) & FR_RXFE);
}

static int
pl011_getc(struct uart *u)
{
	return (unsigned char)reg_read(u, PL011_DR);
}

static unsigned
pl011_intr(struct uart *u)
{
	unsigned int mis;
	unsigned ev = 0;

	if ((mis = reg_read(u, PL011_MIS)) == 0)
		return 0;

	/*
	 * Acknowledge before servicing. The PL011 latches its interrupts,
	 * and a character arriving between the read and the clear would
	 * otherwise have its interrupt cleared without ever being read -
	 * the classic way to lose exactly one keystroke, occasionally.
	 */
	reg_write(u, PL011_ICR, mis);

	if (mis & (INT_RX | INT_RT))
		ev |= UART_EV_RX;
	if (mis & INT_TX)
		ev |= UART_EV_TX;

	return ev;
}

static void
pl011_txintr(struct uart *u, int on)
{
	unsigned int want = on ? (u->imask | INT_TX) : (u->imask & ~INT_TX);

	if (want == u->imask)
		return;
	u->imask = want;
	reg_write(u, PL011_IMSC, u->imask);
}

static void
pl011_brk(struct uart *u, int on)
{
	unsigned int lcrh = reg_read(u, PL011_LCRH);

	reg_write(u, PL011_LCRH, on ? (lcrh | LCRH_BRK) : (lcrh & ~LCRH_BRK));
}

const struct uart_ops pl011_ops = {
	.probe		= pl011_probe,
	.init		= pl011_init,
	.config		= pl011_config,
	.txready	= pl011_txready,
	.txchar		= pl011_putc,
	.rxready	= pl011_rxready,
	.rxchar		= pl011_getc,
	.intr		= pl011_intr,
	.txintr		= pl011_txintr,
	.brk		= pl011_brk,
};
