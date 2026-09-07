/*
 * The 8250 half of the tty serial driver: the DesignWare UART of the RK3566,
 * which is what the BIGTREETECH CB2 puts its console on (uart2, 0xfe660000).
 *
 * The kernel prints on this same port through
 * minix/kernel/arch/aarch64/dw8250.c, which needs three registers and no
 * interrupts. This is the other half of the same part: input, the interrupt
 * identification register, and the FIFOs.
 *
 * Three things about this part that a plain 16550 driver gets wrong:
 *
 *   - The registers are not a byte apart. On the DesignWare variant they sit
 *     on a 32-bit bus four bytes apart, and an access has to be a word. The
 *     tree says so through "reg-shift" and "reg-io-width" (2 and 4 on the
 *     RK3566), so both come from the node rather than from a #define. The
 *     width matters for correctness: a 32-bit read of a byte-wide mapping
 *     would cover four registers at once, and register 0 is the receive
 *     buffer, whose read pops a character off the FIFO.
 *
 *   - Writing to the line control register while the part is busy is
 *     refused, and the refusal arrives as an interrupt: IIR reports 0x7,
 *     "busy detect", which is not one of the interrupts a 16550 has. It is
 *     cleared by reading the USR register, and a driver that does not know
 *     to do that spins in its handler forever. This is the one register
 *     beyond the standard set that this file needs, and the reason
 *     UART_F_DW exists.
 *
 *   - The line is not programmed at all, for the reason dw8250.c gives: the
 *     boot loader has this port running at the rate stdout-path names
 *     (1500000 on the CB2), the reference clock is a board fact nobody here
 *     knows, and setting the divisor means raising DLAB, which swaps two
 *     registers out from under anything printing in between. A console that
 *     already works is not worth that. So config() touches the frame and the
 *     FIFOs and leaves the speed where the firmware left it.
 */

#include <minix/drivers.h>
#include <minix/fdt.h>
#include <sys/termios.h>

#include "tty.h"
#include "uart.h"

/* Register numbers, not offsets: see the file comment. */
#define NS8250_RBR	0	/* receive buffer (read) */
#define NS8250_THR	0	/* transmit holding (write) */
#define NS8250_IER	1	/* interrupt enable */
#define NS8250_IIR	2	/* interrupt identification (read) */
#define NS8250_FCR	2	/* FIFO control (write) */
#define NS8250_LCR	3	/* line control */
#define NS8250_LSR	5	/* line status */
#define NS8250_MSR	6	/* modem status */

/* DesignWare only, and the reason UART_F_DW exists. */
#define DW_USR		31	/* UART status; reading it clears busy */

#define IER_RDI		0x01	/* received data available */
#define IER_THRI	0x02	/* transmit holding register empty */
#define IER_RLSI	0x04	/* receiver line status */

#define IIR_NO_INT	0x01	/* no interrupt pending */
/*
 * Four bits, not three. A 16550 numbers its causes in bits 3:1 and leaves
 * bit 0 to say "none pending", which is why a mask of 0x0e is the usual
 * one; the DesignWare part adds busy detect as 0x07, and under that mask it
 * arrives indistinguishable from receiver line status - whose acknowledgement
 * does not clear it. The handler then has a cause it cannot silence.
 */
#define IIR_ID_MASK	0x0f
#define IIR_ID_MODEM	0x00	/* modem status */
#define IIR_ID_THRI	0x02	/* transmit holding register empty */
#define IIR_ID_RDI	0x04	/* received data available */
#define IIR_ID_RLSI	0x06	/* receiver line status */
#define IIR_ID_BUSY	0x07	/* DesignWare: write to LCR while busy */
#define IIR_ID_RTO	0x0c	/* character timeout */

#define FCR_ENABLE	0x01
#define FCR_CLR_RCVR	0x02
#define FCR_CLR_XMIT	0x04
/*
 * Receive trigger of one character. The quarter-full setting a 16550 driver
 * would reach for means sixteen characters on this part - its FIFO is 64
 * deep, not 16 - and a console that answers after sixteen keystrokes is a
 * console that does not answer. The interrupt count that costs is bounded by
 * the handler, which empties the FIFO in one go however it was woken.
 */
#define FCR_TRIG_ONE	0x00

#define LCR_WLEN8	0x03
#define LCR_SBC		0x40	/* set break control */
#define LCR_DLAB	0x80	/* divisor latch access */

#define LSR_DR		0x01	/* data ready */
#define LSR_BI		0x10	/* break interrupt */
#define LSR_THRE	0x20	/* transmit holding register empty */
#define LSR_TEMT	0x40	/* transmitter completely idle */

/*
 * A DesignWare register block is 0x100 bytes. The generic part needs the
 * first six registers, which at the widest spacing this reader handles
 * (shift 2) is 0x18; anything smaller than that is not a port.
 */
#define NS8250_MIN_SIZE	0x18

static u32_t
reg_read(struct uart *u, unsigned reg)
{
	vir_bytes a = u->regs + ((vir_bytes)reg << u->shift);

	if (u->width == 4)
		return *(volatile u32_t *)a;
	return (u32_t)*(volatile u8_t *)a;
}

static void
reg_write(struct uart *u, unsigned reg, u32_t v)
{
	vir_bytes a = u->regs + ((vir_bytes)reg << u->shift);

	if (u->width == 4)
		*(volatile u32_t *)a = v;
	else
		*(volatile u8_t *)a = (u8_t)v;
}

static int
ns8250_probe(const struct fdt_node *node, struct uart *u)
{
	const void *p;
	u64_t base, size;
	unsigned len;

	if (fdt_node_reg(node, 0, &base, &size) != 0)
		return ENXIO;

	/*
	 * The original part's defaults - one byte per register, byte-wide
	 * access - are what a tree that says nothing means. The RK3566 says
	 * 2 and 4.
	 */
	u->shift = 0;
	u->width = 1;
	if ((p = fdt_getprop(node, "reg-shift", &len)) != NULL && len == 4)
		u->shift = (unsigned)fdt_read_cells(p, 1);
	if ((p = fdt_getprop(node, "reg-io-width", &len)) != NULL && len == 4)
		u->width = (unsigned)fdt_read_cells(p, 1);

	if (u->shift > 2 || (u->width != 1 && u->width != 4))
		return ENXIO;
	if (size < (NS8250_MIN_SIZE << u->shift))
		return ENXIO;

	if (fdt_node_is_compatible(node, "snps,dw-apb-uart"))
		u->flags |= UART_F_DW;

	u->phys = (phys_bytes)base;
	u->size = (size_t)size;

	return OK;
}

static void
ns8250_config(struct uart *u, int up)
{
	/*
	 * Wait for the transmitter to go idle before touching the line
	 * control register. A write to it while the part is busy is refused
	 * and answered with an interrupt instead - see IIR_ID_BUSY - and the
	 * moment this is most likely is exactly the one that happens: a
	 * program prints a prompt and configures the line straight after.
	 * Handling that interrupt is not the same as not causing it.
	 */
	while (!(reg_read(u, NS8250_LSR) & LSR_TEMT))
		;

	/*
	 * DLAB stays down and the divisor is not written: the firmware set
	 * the rate and the kernel has been printing at it since boot. See
	 * the file comment.
	 */
	reg_write(u, NS8250_LCR, LCR_WLEN8);
	reg_write(u, NS8250_FCR,
	    FCR_ENABLE | FCR_CLR_RCVR | FCR_CLR_XMIT | FCR_TRIG_ONE);

	/*
	 * B0 means hang up. There is no modem control line here to drop, so
	 * all "down" can mean is: stop listening.
	 */
	reg_write(u, NS8250_IER, up ? (IER_RDI | IER_RLSI) : 0);
	u->imask = up ? (IER_RDI | IER_RLSI) : 0;
}

static void
ns8250_init(struct uart *u)
{
	ns8250_config(u, TRUE);
}

static int
ns8250_txready(struct uart *u)
{
	return (reg_read(u, NS8250_LSR) & LSR_THRE) != 0;
}

static void
ns8250_putc(struct uart *u, int c)
{
	reg_write(u, NS8250_THR, (u32_t)(unsigned char)c);
}

static int
ns8250_rxready(struct uart *u)
{
	return (reg_read(u, NS8250_LSR) & LSR_DR) != 0;
}

static int
ns8250_getc(struct uart *u)
{
	return (int)(reg_read(u, NS8250_RBR) & 0xff);
}

static unsigned
ns8250_intr(struct uart *u)
{
	u32_t iir;

	iir = reg_read(u, NS8250_IIR);

	if (iir & IIR_NO_INT) {
		/*
		 * The line is asserted and the part says nothing is pending.
		 * On a DesignWare part that is busy detect once more: it is
		 * cleared by reading USR and by nothing else, and reading
		 * IIR - which is what a driver does first - leaves it
		 * standing. The handler is then called again the moment it
		 * returns, forever.
		 *
		 * Measured on the CB2 before this read was here: of twenty
		 * thousand calls, nineteen thousand nine hundred and
		 * ninety-eight arrived with nothing pending. The machine
		 * still booted from the ramdisk and no longer did once the
		 * root was on eMMC and there was real work to do besides.
		 */
		if (u->flags & UART_F_DW)
			(void)reg_read(u, DW_USR);
		return 0;
	}

	switch (iir & IIR_ID_MASK) {
	case IIR_ID_RDI:
		return UART_EV_RX;

	case IIR_ID_RTO:
		/*
		 * The character timeout is cleared by reading RBR, not by
		 * reading IIR - and a DesignWare part will assert it with an
		 * empty receive FIFO. Then LSR says there is no data, the
		 * loop that drains the FIFO reads nothing, the cause stays
		 * asserted, and the next interrupt arrives before this one
		 * has returned.
		 *
		 * That is not a slow console: it is a machine with one
		 * runnable process. tty stays ready forever, every other
		 * process keeps its quantum untouched, and the only thing
		 * that still works is echo - which happens inside the
		 * handler. It cost three boots to see, because a system
		 * starved this way looks exactly like a system that hung.
		 *
		 * So read the byte that is not there. Linux carries the same
		 * workaround in dw8250_handle_irq() for the same part.
		 */
		if ((reg_read(u, NS8250_LSR) & (LSR_DR | LSR_BI)) == 0) {
			(void)reg_read(u, NS8250_RBR);
			return UART_EV_AGAIN;
		}
		return UART_EV_RX;

	case IIR_ID_THRI:
		/* Reading IIR acknowledged it. */
		return UART_EV_TX;

	case IIR_ID_RLSI:
		/* Cleared by reading LSR; the error itself is not acted on. */
		(void)reg_read(u, NS8250_LSR);
		return UART_EV_AGAIN;

	case IIR_ID_BUSY:
		/*
		 * DesignWare only: a write to LCR while the transmitter was
		 * busy. Reading USR is what clears it, and not reading it
		 * means this handler is called again immediately, forever -
		 * with the console alive for output, which needs no
		 * interrupt, and deaf.
		 */
		if (u->flags & UART_F_DW)
			(void)reg_read(u, DW_USR);
		return UART_EV_AGAIN;

	case IIR_ID_MODEM:
		/* Cleared by reading MSR. Nothing here has modem lines. */
		(void)reg_read(u, NS8250_MSR);
		return UART_EV_AGAIN;

	default:
		/*
		 * A cause this driver does not know. Saying "again" would
		 * spin on it until the handler's own limit; saying "nothing
		 * pending" leaves it asserted and the next interrupt finds
		 * it again. Neither is good, and the second is quieter.
		 */
		return 0;
	}
}

static void
ns8250_txintr(struct uart *u, int on)
{
	u32_t want = on ? (u->imask | IER_THRI) : (u->imask & ~IER_THRI);

	if (want == u->imask)
		return;
	u->imask = want;
	reg_write(u, NS8250_IER, u->imask);
}

static void
ns8250_brk(struct uart *u, int on)
{
	u32_t lcr = reg_read(u, NS8250_LCR);

	reg_write(u, NS8250_LCR, on ? (lcr | LCR_SBC) : (lcr & ~LCR_SBC));
}

const struct uart_ops ns8250_ops = {
	.probe		= ns8250_probe,
	.init		= ns8250_init,
	.config		= ns8250_config,
	.txready	= ns8250_txready,
	.txchar		= ns8250_putc,
	.rxready	= ns8250_rxready,
	.rxchar		= ns8250_getc,
	.intr		= ns8250_intr,
	.txintr		= ns8250_txintr,
	.brk		= ns8250_brk,
};
