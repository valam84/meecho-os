#ifndef _AARCH64_SERIAL_H_
#define _AARCH64_SERIAL_H_

/*
 * The two console UARTs this architecture meets, and the registers of each.
 *
 * Which one a machine has is not a build-time fact: QEMU's virt machine has a
 * PL011, the RK3566 on the BIGTREETECH CB2 has a DesignWare 8250, and one
 * kernel has to talk on both - the development cycle runs in QEMU and the
 * target is the board. serial.c reads the device tree to decide; these are
 * only the register layouts.
 *
 * The 8250 is described in registers rather than byte offsets because its
 * spacing is a property of the wiring: the device tree's reg-shift says how
 * far apart consecutive registers sit, and reg-io-width how wide an access
 * to one has to be. On the RK3566 they are 2 and 4, so register n lives at
 * base + (n << 2) and must be read as a word.
 */

/* Which driver the device tree named. Kept as an int, not a function table:
 * this is decided in pre_init(), before the MMU is on, and a pointer stored
 * there would be a physical address that stops existing after the move to
 * the upper half. gic.c carries the same rule and says why. */
#define SERIAL_NONE	0
#define SERIAL_PL011	1
#define SERIAL_8250	2

/* ======================================================================
 * PL011 -- QEMU virt, and the BCM2711
 * ====================================================================== */

#define PL011_DR		0x000	/* data */
#define PL011_FR		0x018	/* flag */
#define PL011_IBRD		0x024	/* integer baud rate divisor */
#define PL011_FBRD		0x028	/* fractional baud rate divisor */
#define PL011_LCRH		0x02c	/* line control */
#define PL011_CR		0x030	/* control */
#define PL011_IMSC		0x038	/* interrupt mask set/clear */
#define PL011_ICR		0x044	/* interrupt clear */

/* PL011_FR bits */
#define PL011_FR_BUSY		(1 << 3)
#define PL011_FR_TXFF		(1 << 5)	/* transmit FIFO full */

/* PL011_LCRH bits */
#define PL011_LCRH_FEN		(1 << 4)	/* enable FIFOs */
#define PL011_LCRH_WLEN_8	(3 << 5)

/* PL011_CR bits */
#define PL011_CR_UARTEN		(1 << 0)
#define PL011_CR_TXE		(1 << 8)
#define PL011_CR_RXE		(1 << 9)

/* All interrupts, for masking and clearing */
#define PL011_INT_ALL		0x7ff

/* ======================================================================
 * 8250 / 16550 -- the DesignWare variant on the RK3566, register numbers
 * ====================================================================== */

#define NS8250_THR		0	/* transmit holding (write) */
#define NS8250_LSR		5	/* line status */

/* NS8250_LSR bits */
#define NS8250_LSR_THRE		(1 << 5)	/* holding register empty */
#define NS8250_LSR_TEMT		(1 << 6)	/* transmitter empty */

void pl011_init(vir_bytes base);
void pl011_putc(vir_bytes base, char c);

void ns8250_init(vir_bytes base, unsigned shift, unsigned width);
void ns8250_putc(vir_bytes base, unsigned shift, unsigned width, char c);

#endif /* _AARCH64_SERIAL_H_ */
