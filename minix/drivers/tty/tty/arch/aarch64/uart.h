#ifndef _TTY_AARCH64_UART_H
#define _TTY_AARCH64_UART_H

/*
 * The contract between rs232.c and a UART.
 *
 * rs232.c owns everything tty.c can see - the circular buffers, the ostate
 * bits, the entry points, the interrupt hook - and none of it depends on
 * which part is at the other end of the wire. What does depend on the part
 * is a dozen small operations: is there room to send, is there a byte to
 * read, which interrupt was that, turn the transmit interrupt on. Those are
 * this table, and each UART is a file of them beside this one.
 *
 * Two parts so far, because the two machines this port runs on have one
 * each: QEMU's virt machine has a PL011, the RK3566 on the BIGTREETECH CB2 a
 * DesignWare 8250. The kernel's own console driver draws the same line for
 * the same reason (minix/kernel/arch/aarch64/serial.h); this is the tty
 * side of it.
 */

#include <minix/fdt.h>

struct uart;

struct uart_ops {
	/*
	 * Read from the node whatever the part needs beyond "reg" and
	 * "interrupts", which rs232.c has already taken. Returns OK, or an
	 * error if the node is not one this driver can drive after all.
	 */
	int (*probe)(const struct fdt_node *node, struct uart *u);

	/*
	 * The registers are mapped: bring the line to a known state and
	 * unmask receive interrupts. The rate is not touched - see
	 * rs232.c for why - so this is the FIFOs, the frame, and the masks.
	 */
	void (*init)(struct uart *u);

	/*
	 * Reapply the line settings after termios changed, once the output
	 * has drained. up is false when the new speed is B0, which on a
	 * modem line means hang up and here means leave the port off.
	 */
	void (*config)(struct uart *u, int up);

	int (*txready)(struct uart *u);		/* room for one more byte */
	void (*txchar)(struct uart *u, int c);
	int (*rxready)(struct uart *u);		/* a byte is waiting */
	int (*rxchar)(struct uart *u);

	/*
	 * Which interrupts are asserted, acknowledged so that the same
	 * ones are not reported twice. A bitmask of UART_EV_*; zero means
	 * nothing is pending and the handler can stop asking.
	 */
	unsigned (*intr)(struct uart *u);

	/* Unmask (on) or mask the transmit interrupt. */
	void (*txintr)(struct uart *u, int on);

	/* Start (on) or end a break condition. */
	void (*brk)(struct uart *u, int on);
};

#define UART_EV_RX	0x01	/* receive data, or receive timeout */
#define UART_EV_TX	0x02	/* room to transmit */
#define UART_EV_AGAIN	0x04	/* something was acknowledged; ask again */

/* u->flags */
#define UART_F_DW	0x01	/* the DesignWare variant of the 8250 */

struct uart {
	const struct uart_ops *ops;

	phys_bytes phys;		/* the node's "reg" */
	size_t size;
	vir_bytes regs;			/* the same, mapped */
	int irq;			/* GIC line */

	/*
	 * How far apart consecutive registers sit and how wide an access
	 * has to be: "reg-shift" and "reg-io-width" of an 8250 node. The
	 * PL011 has fixed offsets and word registers and ignores both.
	 */
	unsigned shift;
	unsigned width;

	unsigned flags;			/* UART_F_* */
	unsigned imask;			/* shadow of the interrupt mask */
};

extern const struct uart_ops pl011_ops;
extern const struct uart_ops ns8250_ops;

#endif /* _TTY_AARCH64_UART_H */
