/*
 * The serial line tty drives, and how it finds it.
 *
 * This is the console: there is no video console on this port, so everything
 * a user types and sees goes through here. The structure - the circular input
 * and output buffers, the ostate bits, the entry points tty installs - is the
 * one arch/earm/rs232.c uses, because that is the contract tty.c expects and
 * it has been debugged.
 *
 *
 * Which UART, and how it is found
 * -------------------------------
 * There is no table of boards here. The two machines this port runs on have
 * different parts on their consoles - a PL011 on QEMU's virt, a DesignWare
 * 8250 on the RK3566 of the BIGTREETECH CB2 - and the register layouts differ
 * enough to be separate files (pl011.c, ns8250.c) behind the small table in
 * uart.h. Which of them a machine wants, and at which address, comes from the
 * device tree, the way every other driver on this port asks: RS grants the
 * registers and the interrupt of nodes named in system.conf, and the driver
 * walks the same tree to find them (PORTING-LOG.md, stage 7.2).
 *
 * The node it looks for is the one /chosen/stdout-path names, resolved
 * through /aliases - the same three passes the kernel's console does in
 * minix/kernel/arch/aarch64/serial.c, and for the same reason: a board has
 * several UARTs and only one of them is wired to a connector. The CB2 has
 * ten, and the first in the tree is not the console. Taking the first UART
 * found would print into a header nobody has a cable on.
 *
 * That makes tty00 the console and leaves tty01..tty03 without devices. A
 * board's other ports are reachable the same way once something wants them -
 * a second entry in /aliases, or a boot argument naming a node - but nothing
 * does yet, and a line with no device is a line tty reports as ENXIO rather
 * than one that silently reads a port that is not there.
 *
 *
 * What is not programmed
 * ----------------------
 * The baud rate. The loader configured this port, the kernel has been
 * printing on it since boot, and the divisor depends on the UART reference
 * clock - a board fact this driver has no way to learn. Changing the rate
 * underneath the kernel would garble everything already in flight. So the
 * rate stdout-path names is what termios reports and what the line keeps;
 * see the kernel's dw8250.c for the same decision on the other side.
 */

#include <minix/drivers.h>
#include <minix/ds.h>
#include <minix/fdt.h>
#include <minix/vm.h>
#include <sys/mman.h>
#include <sys/termios.h>
#include <assert.h>
#include <stdlib.h>

#include "tty.h"
#include "uart.h"

/* Input buffer high and low water marks, as on ARM. */
#define RS_IBUFSIZE	1024
#define RS_OBUFSIZE	1024
#define RS_IHIGHWATER	(3 * RS_IBUFSIZE / 4)
#define RS_ILOWWATER	(1 * RS_IBUFSIZE / 4)
#define RS_OLOWWATER	(1 * RS_OBUFSIZE / 4)

/* The rate to report when the device tree names none. */
#define DFLT_BAUD	B115200

/*
 * How many times round the interrupt handler before giving up on a line that
 * will not go quiet. A UART asserting a cause this driver does not clear
 * would otherwise hang the whole tty server, and a hung tty on this port is a
 * machine with no console at all.
 */
#define INTR_ROUNDS	32

/* buflen() and bufend() come from tty.h. */

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

	struct uart uart;		/* the part, and where it is */

	int irq_hook_id;
	int irq_hook_kernel_id;

	char ibuf[RS_IBUFSIZE];
	char obuf[RS_OBUFSIZE];
} rs232_t;

static rs232_t rs_lines[NR_RS_LINES];

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

/*
 * There are no modem control lines on these ports - neither QEMU's PL011 nor
 * the CB2's header has any connected - so the device is always ready to send
 * and never hangs up. ARM tracks CTS and DCD here; there is nothing to track.
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
 *			finding the console in the tree			     *
 *===========================================================================*/
/* Long enough for the paths that occur: "/serial@fe660000" and the like. */
#define CONSOLE_PATH_MAX	96

struct ser_scan {
	/* Passes one and two: what /chosen says, resolved through /aliases. */
	char path[CONSOLE_PATH_MAX];

	/* The speed after the ':' of stdout-path, or zero. */
	unsigned speed;

	/* Pass three: the node to match, and what was found on it. */
	const char *want;
	const struct uart_ops *ops;
	struct uart *u;
	int irq;
	int found;
};

/*
 * Copy src into dst, stopping at NUL or at stop. Truncation is not an error
 * worth reporting: a path this long is a tree we would not understand anyway,
 * and the match in pass three simply fails.
 */
static void
copy_token(char *dst, size_t size, const char *src, char stop)
{
	size_t i;

	for (i = 0; i + 1 < size && src[i] != '\0' && src[i] != stop; i++)
		dst[i] = src[i];
	dst[i] = '\0';
}

/* The part of a device tree path after the final slash: its node name. */
static const char *
last_component(const char *path)
{
	const char *p, *last = path;

	for (p = path; *p != '\0'; p++)
		if (*p == '/')
			last = p + 1;

	return last;
}

/*
 * Pass one: /chosen/stdout-path names the console, optionally followed by
 * ':' and the line settings - "serial2:1500000n8" on the CB2. The settings
 * are not applied, only reported: see the file comment.
 */
static int
scan_chosen(void *cookie, int depth, const char *name,
	const struct fdt_node *node)
{
	struct ser_scan *s = cookie;
	const char *p;
	unsigned len;

	if (depth != 1 || strcmp(name, "chosen") != 0)
		return 0;

	if ((p = fdt_getprop(node, "stdout-path", &len)) == NULL)
		p = fdt_getprop(node, "linux,stdout-path", &len);
	if (p == NULL || len == 0)
		return 1;

	copy_token(s->path, sizeof(s->path), p, ':');

	/* The digits after the colon, if there are any, are the speed. */
	while (*p != '\0' && *p != ':')
		p++;
	if (*p == ':')
		for (p++; *p >= '0' && *p <= '9'; p++)
			s->speed = s->speed * 10 + (unsigned)(*p - '0');

	return 1;
}

/*
 * Pass two: turn an alias into a path. QEMU writes stdout-path as a path
 * already ("/pl011@9000000") and skips this; the CB2 writes "serial2", which
 * /aliases resolves to "/serial@fe660000".
 */
static int
scan_alias(void *cookie, int depth, const char *name,
	const struct fdt_node *node)
{
	struct ser_scan *s = cookie;
	const char *p;
	unsigned len;

	if (depth != 1 || strcmp(name, "aliases") != 0)
		return 0;

	if ((p = fdt_getprop(node, s->path, &len)) != NULL && len > 0)
		copy_token(s->path, sizeof(s->path), p, '\0');

	return 1;
}

/* Is this node a UART one of the files beside this one drives? */
static const struct uart_ops *
uart_ops_for(const struct fdt_node *node)
{
	if (fdt_node_is_compatible(node, "arm,pl011"))
		return &pl011_ops;
	if (fdt_node_is_compatible(node, "snps,dw-apb-uart") ||
	    fdt_node_is_compatible(node, "ns16550a") ||
	    fdt_node_is_compatible(node, "ns16550"))
		return &ns8250_ops;
	return NULL;
}

/*
 * Pass three: take the node /chosen named, and no other. Without a name - a
 * tree with no /chosen, which QEMU's virt does have but a hand-built blob
 * might not - any UART is better than none.
 */
static int
scan_uart(void *cookie, int depth, const char *name,
	const struct fdt_node *node)
{
	struct ser_scan *s = cookie;
	const struct uart_ops *ops;

	if (depth == 0)
		return 0;

	if (s->want != NULL && strcmp(name, s->want) != 0)
		return 0;

	if ((ops = uart_ops_for(node)) != NULL &&
	    ops->probe(node, s->u) == OK) {
		s->ops = ops;
		s->irq = fdt_node_gic_irq(node, 0);
		s->found = 1;
	}

	/*
	 * With a name, stop either way: the tree named this node as the
	 * console, so if it is not a UART this code knows, there is nothing
	 * better further down.
	 */
	return s->want != NULL || s->found;
}

/*
 * Fill in rs->uart from the device tree. Returns FALSE when this machine has
 * no console UART this driver knows, which is a machine tty runs on without a
 * serial line rather than one it refuses to start on.
 */
static int
find_console(rs232_t *rs, unsigned *speed)
{
	struct ser_scan s;
	void *dtb;

	memset(&s, 0, sizeof(s));
	s.u = &rs->uart;

	if ((dtb = fdt_fetch()) == NULL)
		return FALSE;

	(void)fdt_walk(dtb, scan_chosen, &s);

	/*
	 * A path starting with '/' is the node itself; anything else is an
	 * alias, and /aliases turns it into a path.
	 */
	if (s.path[0] != '\0' && s.path[0] != '/')
		(void)fdt_walk(dtb, scan_alias, &s);

	if (s.path[0] == '/')
		s.want = last_component(s.path);

	(void)fdt_walk(dtb, scan_uart, &s);

	free(dtb);

	if (!s.found)
		return FALSE;

	rs->uart.ops = s.ops;
	rs->uart.irq = s.irq;
	*speed = s.speed;

	return TRUE;
}

/*===========================================================================*
 *				rs_config				     *
 *===========================================================================*/
static void
rs_config(rs232_t *rs)
{
/* Set line parameters from the tty's termios. */
	tty_t *tp = rs->tty;

	/* Speed zero means hang up, and here that means stay down. */
	rs->uart.ops->config(&rs->uart, tp->tty_termios.c_ospeed != B0);
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
	char l[10];
	unsigned speed = 0;
	struct minix_mem_range mr;
	void *v;

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

	/*
	 * Only the console has a device: see the file comment. The other
	 * lines stay inactive, and tty reports them as ENXIO.
	 */
	if (line != 0)
		return;

	rs = tp->tty_priv = &rs_lines[line];
	rs->tty = tp;
	rs->ihead = rs->itail = rs->ibuf;
	rs->ohead = rs->otail = rs->obuf;
	rs->icount = rs->ocount = 0;

	if (!find_console(rs, &speed)) {
		printf("TTY: no serial console in the device tree\n");
		tp->tty_priv = NULL;
		return;
	}

	/*
	 * Ask for access to the registers, then map them: this driver found
	 * its own device, so it is the one that knows which range to ask for.
	 */
	mr.mr_base = rs->uart.phys;
	mr.mr_limit = rs->uart.phys + rs->uart.size;
	if (sys_privctl(SELF, SYS_PRIV_ADD_MEM, &mr) != OK)
		panic("TTY: no permission for the UART registers");

	v = vm_map_phys(SELF, (void *)rs->uart.phys, rs->uart.size);
	if (v == MAP_FAILED)
		panic("TTY: unable to map the UART registers");
	rs->uart.regs = (vir_bytes)v;

	/*
	 * The rate the loader chose is the rate we keep, and the rate termios
	 * reports. See the file comment.
	 */
	tp->tty_termios.c_ospeed = tp->tty_termios.c_ispeed =
	    (speed != 0) ? speed : DFLT_BAUD;

	/*
	 * Hook id line + 1, because a hook id of zero is indistinguishable
	 * from "no hook" in the notification bitmap. ARM does the same and
	 * says so.
	 */
	rs->irq_hook_kernel_id = rs->irq_hook_id = line + 1;

	if (rs->uart.irq < 0) {
		/*
		 * A console with no usable interrupt still prints; it just
		 * cannot be typed on. Saying so beats a silent half-console.
		 */
		printf("RS232: no interrupt in the device tree; input off\n");
	} else if (sys_irqsetpolicy(rs->uart.irq, 0,
	    &rs->irq_hook_kernel_id) != OK) {
		printf("RS232: couldn't obtain hook for irq %d\n",
		    rs->uart.irq);
	} else {
		if (sys_irqenable(&rs->irq_hook_kernel_id) != OK)
			printf("RS232: couldn't enable irq %d\n",
			    rs->uart.irq);
		rs_irq_set |= (1 << rs->irq_hook_id);
	}

	rs->ostate = ODEVREADY | OSWREADY;

	rs->uart.ops->init(&rs->uart);

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
	if (rs->uart.ops->txready(&rs->uart))
		write_chars(rs);
}

/*===========================================================================*
 *				rs_break_on				     *
 *===========================================================================*/
static int
rs_break_on(tty_t *tp, int UNUSED(dummy))
{
	rs232_t *rs = tp->tty_priv;

	rs->uart.ops->brk(&rs->uart, TRUE);

	return 0;	/* dummy */
}

/*===========================================================================*
 *				rs_break_off				     *
 *===========================================================================*/
static int
rs_break_off(tty_t *tp, int UNUSED(dummy))
{
	rs232_t *rs = tp->tty_priv;

	rs->uart.ops->brk(&rs->uart, FALSE);

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
		if (rs->uart.ops == NULL)
			continue;
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
	unsigned ev;
	int round;

	/*
	 * An 8250 reports one cause at a time and asserts the line again for
	 * the next, so this is a loop where a PL011 would need a single pass;
	 * the bound is there because a cause this driver fails to clear would
	 * otherwise be an infinite one.
	 */
	for (round = 0; round < INTR_ROUNDS; round++) {
		if ((ev = rs->uart.ops->intr(&rs->uart)) == 0)
			return;

		if (ev & UART_EV_RX)
			read_chars(rs);

		if (ev & UART_EV_TX)
			write_chars(rs);
	}
}

/*===========================================================================*
 *				read_chars				     *
 *===========================================================================*/
static void
read_chars(rs232_t *rs)
{
	unsigned char c;

	while (rs->uart.ops->rxready(&rs->uart)) {
		c = (unsigned char)rs->uart.ops->rxchar(&rs->uart);

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
 * left to send: both parts assert it whenever the FIFO is below its level, so
 * leaving it on with nothing to write is an interrupt storm.
 */
	if (rs->ostate < (ODEVREADY | OQUEUED | OSWREADY))
		return;

	while (rs->ocount > 0 && rs->uart.ops->txready(&rs->uart)) {
		rs->uart.ops->txchar(&rs->uart, (unsigned char)*rs->otail);
		if (++rs->otail == bufend(rs->obuf))
			rs->otail = rs->obuf;

		if (--rs->ocount == 0) {
			/* Output done: turn ODONE on and OQUEUED off. */
			rs->ostate ^= (ODONE | OQUEUED);
			rs->tty->tty_events = 1;
			rs->uart.ops->txintr(&rs->uart, FALSE);
			return;
		}

		if (rs->ocount == RS_OLOWWATER)
			rs->tty->tty_events = 1;
	}

	/* Still something to send: ask to be told when there is room. */
	if (rs->ocount > 0)
		rs->uart.ops->txintr(&rs->uart, TRUE);
}
