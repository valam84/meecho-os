/*
 * PL011 serial line for tty.
 *
 * This is the console: there is no video console on this port, so everything
 * a user types and sees goes through here. The structure - the circular input
 * and output buffers, the ostate bits, the entry points tty installs - is the
 * one arch/earm/rs232.c uses, because that is the contract tty.c expects and
 * it has been debugged. What differs is the device underneath: a PL011 rather
 * than an OMAP UART, and no modem control lines at all.
 *
 *
 * Which UART, and on which board
 * ------------------------------
 * The address and interrupt come from a table indexed by the board, the way
 * ARM does it, because a driver in user space cannot read the device tree -
 * the kernel does that, and what it learns does not reach here.
 *
 * Only QEMU's virt machine is in the table so far. A second entry is where a
 * real board goes, and it will not necessarily be a PL011: the part is common
 * on ARM SoCs, but plenty of them - Rockchip and Allwinner among them - use a
 * DesignWare 8250 instead, which is a different register layout and would be
 * a second file rather than a second row. The split above is drawn so that
 * this file stays PL011 and the table decides who uses it.
 */

#include <minix/drivers.h>
#include <minix/board.h>
#include <minix/ds.h>
#include <minix/vm.h>
#include <sys/mman.h>
#include <sys/termios.h>
#include <assert.h>

#include "tty.h"

/* Input buffer high and low water marks, as on ARM. */
#define RS_IBUFSIZE	1024
#define RS_OBUFSIZE	1024
#define RS_IHIGHWATER	(3 * RS_IBUFSIZE / 4)
#define RS_ILOWWATER	(1 * RS_IBUFSIZE / 4)
#define RS_OLOWWATER	(1 * RS_OBUFSIZE / 4)

/*
 * The rate the firmware left the port at. It is not programmed - see
 * rs_config() - so this only tells termios what to report.
 */
#define DFLT_BAUD	B115200

/* buflen() and bufend() come from tty.h. */

/*
 * PL011 registers, by byte offset. The same set the kernel's own console
 * driver uses; see minix/kernel/arch/aarch64/bsp/qemu-virt/virt_registers.h,
 * which is where these came from.
 */
#define PL011_DR	0x000	/* data */
#define PL011_FR	0x018	/* flag */
#define PL011_IBRD	0x024	/* integer baud rate divisor */
#define PL011_FBRD	0x028	/* fractional baud rate divisor */
#define PL011_LCRH	0x02c	/* line control */
#define PL011_CR	0x030	/* control */
#define PL011_IFLS	0x034	/* interrupt FIFO level select */
#define PL011_IMSC	0x038	/* interrupt mask set/clear */
#define PL011_RIS	0x03c	/* raw interrupt status */
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

#define UART_MAP_SIZE	0x1000

typedef struct rs232 {
	tty_t *tty;			/* associated TTY structure */

	int icount;			/* bytes in the input buffer */
	char *ihead;			/* next free spot */
	char *itail;			/* first byte to give to TTY */
	char idevready;			/* ready to receive */

	unsigned char ostate;		/* combination of the flags below */
#define ODONE		0x01		/* output completed */
#define ORAW		0x02		/* raw mode, xoff disabled */
#define OQUEUED		0x20		/* output buffer not empty */
#define OSWREADY	0x40		/* other end has not sent xoff */
#define ODEVREADY	0x80		/* device ready to send */

	unsigned char oxoff;		/* char that stops output */
	char inhibited;			/* output inhibited? */
	char drain;			/* drain output, then reconfigure */
	int ocount;			/* bytes in the output buffer */
	char *ohead;
	char *otail;

	vir_bytes base;			/* mapped register base */
	unsigned int imsc;		/* copy of the interrupt mask */

	int irq;
	int irq_hook_id;
	int irq_hook_kernel_id;

	char ibuf[RS_IBUFSIZE];
	char obuf[RS_OBUFSIZE];
} rs232_t;

static rs232_t rs_lines[NR_RS_LINES];

typedef struct uart_port {
	phys_bytes base_addr;
	int irq;
} uart_port_t;

/*
 * QEMU virt: PL011 at 0x09000000, GIC INTID 33 - the device tree calls it
 * GIC_SPI 1, and an SPI's INTID is its number plus 32. The remaining lines
 * are absent; the machine has one UART.
 */
static uart_port_t qemu_virt_ports[] = {
	{ 0x09000000, 33 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 }
};

static int rs_write(tty_t *tp, int try);
static void rs_echo(tty_t *tp, int c);
static int rs_ioctl(tty_t *tp, int try);
static void rs_config(rs232_t *rs);
static int rs_read(tty_t *tp, int try);
static int rs_icancel(tty_t *tp, int try);
static int rs_ocancel(tty_t *tp, int try);
static void rs_ostart(rs232_t *rs);
static int rs_break_on(tty_t *tp, int try);
static int rs_break_off(tty_t *tp, int try);
static int rs_close(tty_t *tp, int try);
static int rs_open(tty_t *tp, int try);
static void rs232_handler(rs232_t *rs);
static void write_chars(rs232_t *rs);
static void read_chars(rs232_t *rs);

static inline unsigned int
serial_in(rs232_t *rs, int offset)
{
	return *(volatile unsigned int *)(rs->base + offset);
}

static inline void
serial_out(rs232_t *rs, int offset, unsigned int val)
{
	*(volatile unsigned int *)(rs->base + offset) = val;
}

/* Room in the transmit FIFO? */
#define txready(rs)	(!(serial_in(rs, PL011_FR) & FR_TXFF))

/*
 * There are no modem control lines on this port - QEMU's PL011 has none
 * connected - so the device is always ready to send and never hangs up. ARM
 * tracks CTS and DCD here; there is nothing to track.
 */
static void
istart(rs232_t *rs)
{
	rs->idevready = TRUE;
}

static void
istop(rs232_t *rs)
{
	rs->idevready = FALSE;
}

/*===========================================================================*
 *				rs_config				     *
 *===========================================================================*/
static void
rs_config(rs232_t *rs)
{
/* Set line parameters from the tty's termios. */
	tty_t *tp = rs->tty;
	unsigned int cr;

	/* Quiesce the port before touching its configuration. */
	cr = serial_in(rs, PL011_CR);
	serial_out(rs, PL011_CR, 0);
	while (serial_in(rs, PL011_FR) & FR_BUSY)
		;

	/*
	 * The baud rate divisors are deliberately left alone. QEMU ignores
	 * them entirely, and on a real board they depend on the UART
	 * reference clock, which this driver has no way to learn - the kernel
	 * reads the device tree, and what it finds does not reach user space.
	 * Until it does, the rate the firmware set is the rate we keep.
	 */
	serial_out(rs, PL011_LCRH, LCRH_WLEN_8 | LCRH_FEN);
	serial_out(rs, PL011_IFLS, IFLS_RX_QUARTER | IFLS_TX_QUARTER);

	if (tp->tty_termios.c_ospeed == B0) {
		/* Speed zero means hang up, and here that means stay down. */
		return;
	}

	serial_out(rs, PL011_CR, cr | CR_UARTEN | CR_TXE | CR_RXE);
}

/*===========================================================================*
 *				rs_init					     *
 *===========================================================================*/
void
rs_init(tty_t *tp)
{
/* Initialize one line. */
	rs232_t *rs;
	int line;
	uart_port_t port;
	char l[10];
	struct minix_mem_range mr;
	struct machine machine;

	line = tp - &tty_table[NR_CONS];

	/*
	 * If the kernel is using this line for its own printing, leave it
	 * alone: two drivers on one FIFO produce interleaved nonsense, and
	 * the kernel cannot be asked to stop.
	 */
	if (env_get_param(SERVARNAME, l, sizeof(l) - 1) == OK &&
	    atoi(l) == line) {
		printf("TTY: rs232 line %d not initialized (used by kernel)\n",
		    line);
		return;
	}

	sys_getmachine(&machine);

	if (BOARD_IS_QEMU_VIRT(machine.board_id))
		port = qemu_virt_ports[line];
	else
		return;

	if (port.base_addr == 0)
		return;

	rs = tp->tty_priv = &rs_lines[line];
	rs->tty = tp;
	rs->ihead = rs->itail = rs->ibuf;
	rs->ohead = rs->otail = rs->obuf;
	rs->icount = rs->ocount = 0;

	/* Ask for access to the registers, then map them. */
	mr.mr_base = port.base_addr;
	mr.mr_limit = port.base_addr + UART_MAP_SIZE;
	if (sys_privctl(SELF, SYS_PRIV_ADD_MEM, &mr) != OK)
		panic("TTY: no permission for the UART registers");

	rs->base = (vir_bytes)vm_map_phys(SELF, (void *)port.base_addr,
	    UART_MAP_SIZE);
	if (rs->base == (vir_bytes)MAP_FAILED)
		panic("TTY: unable to map the UART registers");

	/*
	 * Keep the rate the firmware chose, for the same reason rs_config()
	 * does not program the divisors: the kernel has been printing on this
	 * port since boot, and changing the rate underneath it would garble
	 * everything already in flight.
	 */
	tp->tty_termios.c_ospeed = DFLT_BAUD;

	rs->irq = port.irq;
	/*
	 * Hook id line + 1, because a hook id of zero is indistinguishable
	 * from "no hook" in the notification bitmap. ARM does the same and
	 * says so.
	 */
	rs->irq_hook_kernel_id = rs->irq_hook_id = line + 1;

	if (sys_irqsetpolicy(rs->irq, 0, &rs->irq_hook_kernel_id) != OK) {
		printf("RS232: couldn't obtain hook for irq %d\n", rs->irq);
	} else {
		if (sys_irqenable(&rs->irq_hook_kernel_id) != OK)
			printf("RS232: couldn't enable irq %d\n", rs->irq);
	}

	rs_irq_set |= (1 << rs->irq_hook_id);

	/* Clear anything stale, then take receive and receive-timeout. */
	serial_out(rs, PL011_ICR, INT_ALL);
	rs->imsc = INT_RX | INT_RT;
	serial_out(rs, PL011_IMSC, rs->imsc);

	rs->ostate = ODEVREADY | OSWREADY;

	rs_config(rs);

	tp->tty_devread = rs_read;
	tp->tty_devwrite = rs_write;
	tp->tty_echo = rs_echo;
	tp->tty_icancel = rs_icancel;
	tp->tty_ocancel = rs_ocancel;
	tp->tty_ioctl = rs_ioctl;
	tp->tty_break_on = rs_break_on;
	tp->tty_break_off = rs_break_off;
	tp->tty_open = rs_open;
	tp->tty_close = rs_close;

	istart(rs);
}

/*===========================================================================*
 *				rs_write				     *
 *===========================================================================*/
static int
rs_write(register tty_t *tp, int try)
{
	rs232_t *rs = tp->tty_priv;
	int r, count, ocount;

	if (rs->inhibited != tp->tty_inhibited) {
		rs->ostate |= OSWREADY;
		if (tp->tty_inhibited)
			rs->ostate &= ~OSWREADY;
		rs->inhibited = tp->tty_inhibited;
	}

	if (rs->drain) {
		if (rs->ocount > 0)
			return 0;
		rs->drain = FALSE;
		rs_config(rs);
	}

	for (;;) {
		ocount = buflen(rs->obuf) - rs->ocount;
		count = bufend(rs->obuf) - rs->ohead;
		if (count > ocount) count = ocount;
		if (count > tp->tty_outleft) count = tp->tty_outleft;
		if (count == 0 || tp->tty_inhibited) {
			if (try) return 0;
			break;
		}
		if (try) return 1;

		if (tp->tty_outcaller == KERNEL) {
			/* Printing on the kernel's behalf. */
			memcpy(rs->ohead,
			    (char *)tp->tty_outgrant + tp->tty_outcum, count);
		} else {
			if ((r = sys_safecopyfrom(tp->tty_outcaller,
			    tp->tty_outgrant, tp->tty_outcum,
			    (vir_bytes)rs->ohead, count)) != OK)
				return 0;
		}

		out_process(tp, rs->obuf, rs->ohead, bufend(rs->obuf), &count,
		    &ocount);
		if (count == 0)
			break;

		tp->tty_reprint = TRUE;

		rs->ocount += ocount;
		rs_ostart(rs);
		if ((rs->ohead += ocount) >= bufend(rs->obuf))
			rs->ohead -= buflen(rs->obuf);
		tp->tty_outcum += count;
		if ((tp->tty_outleft -= count) == 0) {
			if (tp->tty_outcaller != KERNEL)
				chardriver_reply_task(tp->tty_outcaller,
				    tp->tty_outid, tp->tty_outcum);
			tp->tty_outcum = 0;
			tp->tty_outcaller = NONE;
		}
	}

	if (tp->tty_outleft > 0 && tp->tty_termios.c_ospeed == B0) {
		/* The line has hung up. */
		if (tp->tty_outcaller != KERNEL)
			chardriver_reply_task(tp->tty_outcaller, tp->tty_outid,
			    EIO);
		tp->tty_outleft = tp->tty_outcum = 0;
		tp->tty_outcaller = NONE;
	}

	return 1;
}

/*===========================================================================*
 *				rs_echo					     *
 *===========================================================================*/
static void
rs_echo(tty_t *tp, int character)
{
	rs232_t *rs = tp->tty_priv;
	int count, ocount;

	ocount = buflen(rs->obuf) - rs->ocount;
	if (ocount == 0)
		return;			/* output buffer full */
	count = 1;
	*rs->ohead = character;

	out_process(tp, rs->obuf, rs->ohead, bufend(rs->obuf), &count, &ocount);
	if (count == 0)
		return;

	rs->ocount += ocount;
	rs_ostart(rs);
	if ((rs->ohead += ocount) >= bufend(rs->obuf))
		rs->ohead -= buflen(rs->obuf);
}

/*===========================================================================*
 *				rs_ioctl				     *
 *===========================================================================*/
static int
rs_ioctl(tty_t *tp, int UNUSED(dummy))
{
/* Reconfigure the line, once whatever is queued has gone out. */
	rs232_t *rs = tp->tty_priv;

	rs->drain = TRUE;

	return 0;	/* dummy */
}

/*===========================================================================*
 *				rs_read					     *
 *===========================================================================*/
static int
rs_read(tty_t *tp, int try)
{
/* Process characters from the circular input buffer. */
	rs232_t *rs = tp->tty_priv;
	int icount, count;

	/*
	 * No carrier detect on this port, so there is no hangup to report -
	 * the CLOCAL branch ARM has here would never fire.
	 */
	if (try)
		return(rs->icount > 0);

	while ((count = rs->icount) > 0) {
		icount = bufend(rs->ibuf) - rs->itail;
		if (count > icount) count = icount;

		if ((count = in_process(tp, rs->itail, count)) == 0)
			break;
		rs->icount -= count;
		if (!rs->idevready && rs->icount < RS_ILOWWATER)
			istart(rs);
		if ((rs->itail += count) == bufend(rs->ibuf))
			rs->itail = rs->ibuf;
	}

	return 0;
}

/*===========================================================================*
 *				rs_icancel				     *
 *===========================================================================*/
static int
rs_icancel(tty_t *tp, int UNUSED(dummy))
{
	rs232_t *rs = tp->tty_priv;

	rs->icount = 0;
	rs->itail = rs->ihead;
	istart(rs);

	return 0;	/* dummy */
}

/*===========================================================================*
 *				rs_ocancel				     *
 *===========================================================================*/
static int
rs_ocancel(tty_t *tp, int UNUSED(dummy))
{
	rs232_t *rs = tp->tty_priv;

	rs->ostate &= ~(ODONE | OQUEUED);
	rs->ocount = 0;
	rs->otail = rs->ohead;

	return 0;	/* dummy */
}

/*===========================================================================*
 *				rs_ostart				     *
 *===========================================================================*/
static void
rs_ostart(rs232_t *rs)
{
/* Something is waiting in the output buffer. */
	rs->ostate |= OQUEUED;
	if (txready(rs))
		write_chars(rs);
}

/*===========================================================================*
 *				rs_break_on				     *
 *===========================================================================*/
static int
rs_break_on(tty_t *tp, int UNUSED(dummy))
{
	rs232_t *rs = tp->tty_priv;

	serial_out(rs, PL011_LCRH, serial_in(rs, PL011_LCRH) | LCRH_BRK);

	return 0;	/* dummy */
}

/*===========================================================================*
 *				rs_break_off				     *
 *===========================================================================*/
static int
rs_break_off(tty_t *tp, int UNUSED(dummy))
{
	rs232_t *rs = tp->tty_priv;

	serial_out(rs, PL011_LCRH, serial_in(rs, PL011_LCRH) & ~LCRH_BRK);

	return 0;	/* dummy */
}

/*===========================================================================*
 *				rs_open					     *
 *===========================================================================*/
static int
rs_open(tty_t *tp, int UNUSED(dummy))
{
/* No carrier to wait for: the line is always up. */
	tp->tty_termios.c_cflag |= CLOCAL;

	return 0;	/* dummy */
}

/*===========================================================================*
 *				rs_close				     *
 *===========================================================================*/
static int
rs_close(tty_t *tp, int UNUSED(dummy))
{
/* Nothing to drop - there is no DTR line here. */
	return 0;	/* dummy */
}

/*===========================================================================*
 *				rs_interrupt				     *
 *===========================================================================*/
void
rs_interrupt(message *m)
{
	unsigned long irq_set;
	int line;
	rs232_t *rs;

	irq_set = m->m_notify.interrupts;
	for (line = 0, rs = rs_lines; line < NR_RS_LINES; line++, rs++) {
		if (irq_set & (1 << rs->irq_hook_id)) {
			rs232_handler(rs);
			if (sys_irqenable(&rs->irq_hook_kernel_id) != OK)
				panic("unable to re-enable interrupts");
		}
	}
}

/*===========================================================================*
 *				rs232_handler				     *
 *===========================================================================*/
static void
rs232_handler(rs232_t *rs)
{
	unsigned int mis;

	mis = serial_in(rs, PL011_MIS);
	if (mis == 0)
		return;

	/*
	 * Acknowledge before servicing. The PL011 latches its interrupts, and
	 * a character arriving between the read and the clear would otherwise
	 * have its interrupt cleared without ever being read - the classic
	 * way to lose exactly one keystroke, occasionally.
	 */
	serial_out(rs, PL011_ICR, mis);

	if (mis & (INT_RX | INT_RT))
		read_chars(rs);

	if (mis & INT_TX)
		write_chars(rs);
}

/*===========================================================================*
 *				read_chars				     *
 *===========================================================================*/
static void
read_chars(rs232_t *rs)
{
	unsigned char c;

	while (!(serial_in(rs, PL011_FR) & FR_RXFE)) {
		c = (unsigned char)serial_in(rs, PL011_DR);

		if (!(rs->ostate & ORAW)) {
			if (c == rs->oxoff) {
				rs->ostate &= ~OSWREADY;
			} else if (!(rs->ostate & OSWREADY)) {
				rs->ostate |= OSWREADY;
			}
		}

		if (rs->icount == buflen(rs->ibuf)) {
			/* No room; drain the FIFO anyway. */
			continue;
		}

		if (++rs->icount == RS_IHIGHWATER && rs->idevready)
			istop(rs);

		*rs->ihead = c;
		if (++rs->ihead == bufend(rs->ibuf))
			rs->ihead = rs->ibuf;

		if (rs->icount == 1)
			rs->tty->tty_events = 1;
	}
}

/*===========================================================================*
 *				write_chars				     *
 *===========================================================================*/
static void
write_chars(rs232_t *rs)
{
/*
 * Push as much as the transmit FIFO will take, and tell tty when the buffer
 * runs dry. The transmit interrupt is only unmasked while there is something
 * left to send: a PL011 asserts it whenever the FIFO is below its level, so
 * leaving it on with nothing to write is an interrupt storm.
 */
	if (rs->ostate < (ODEVREADY | OQUEUED | OSWREADY))
		return;

	while (rs->ocount > 0 && txready(rs)) {
		serial_out(rs, PL011_DR, (unsigned char)*rs->otail);
		if (++rs->otail == bufend(rs->obuf))
			rs->otail = rs->obuf;

		if (--rs->ocount == 0) {
			/* Output done: turn ODONE on and OQUEUED off. */
			rs->ostate ^= (ODONE | OQUEUED);
			rs->tty->tty_events = 1;
			if (rs->imsc & INT_TX) {
				rs->imsc &= ~INT_TX;
				serial_out(rs, PL011_IMSC, rs->imsc);
			}
			return;
		}

		if (rs->ocount == RS_OLOWWATER)
			rs->tty->tty_events = 1;
	}

	/* Still something to send: ask to be told when there is room. */
	if (rs->ocount > 0 && !(rs->imsc & INT_TX)) {
		rs->imsc |= INT_TX;
		serial_out(rs, PL011_IMSC, rs->imsc);
	}
}
