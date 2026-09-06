#ifndef _VIRT_REGISTERS_H_
#define _VIRT_REGISTERS_H_

/*
 * Where the devices of QEMU's virt machine are, and what their registers
 * mean.
 *
 * The addresses are not guesses and not folklore: they come from the machine
 * itself, dumped with
 *
 *	qemu-system-aarch64 -M virt,dumpdtb=virt.dtb ...
 *	dtc -I dtb -O dts virt.dtb
 *
 * which is the only way to be sure, since QEMU is free to move them between
 * releases. The device tree the loader hands the kernel says the same thing,
 * and once it is parsed these become a fallback rather than the source.
 *
 * The PL011 is the same peripheral the BCM2711 has, at a different address,
 * which is why virt is the first target: the device set is the one the
 * Raspberry Pi actually has.
 */

/* ======================================================================
 * PL011 UART0 -- the console
 * ====================================================================== */

#define VIRT_UART0_BASE		0x09000000UL
#define VIRT_UART0_SIZE		0x1000UL
#define VIRT_UART0_IRQ		33	/* GIC_SPI 1 */

/* PL011 register offsets */
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
 * The interrupt controller is not here.
 *
 * It used to be, and the asymmetry with the console above is worth keeping
 * on record. The console has to come up before anything else, because a
 * failure to parse the device tree has to be reportable - bsp_ser_init()
 * runs as the first statement of pre_init(), before the kernel has even
 * recorded where the tree is, so it has no choice but to know its own
 * address. The GIC is needed later, once the tree has been read, so it can
 * be asked instead.
 *
 * Being asked is what eventually took it out of this file: a GIC version and
 * its addresses are properties of the machine rather than of the board this
 * package describes, and this one machine reports either version depending
 * on QEMU gic-version=. It now lives in arch/aarch64/gic.c, gicv2.c and
 * gicv3.c, with its registers in <gic.h>.
 * ====================================================================== */

#endif /* _VIRT_REGISTERS_H_ */
