/*
 * Addresses and register offsets for the QEMU virt machine.
 *
 * Laid out to mirror port/bsp/broadcom/bcm2711_registers.h, because the two
 * boards are closer than they look: virt gives a PL011 and a GICv2 CPU
 * interface, and so does the BCM2711. Keeping the headers parallel is what
 * makes the CM4 bring-up a matter of changing addresses.
 *
 * Every value here was read back out of QEMU rather than remembered:
 *
 *	qemu-system-aarch64 -M virt,dumpdtb=virt.dtb -cpu cortex-a72 ...
 *	dtc -I dtb -O dts virt.dtb
 *
 * which reports intc@8000000 compatible "arm,cortex-a15-gic" - that is
 * GICv2, the same architecture as the CM4's GIC-400.
 *
 * Interrupt numbers are GIC INTIDs. The device tree lists SPIs as
 * <0 n flags> where INTID = n + 32, and PPIs as <1 n flags> where
 * INTID = n + 16.
 */

#ifndef _VIRT_REGISTERS_H_
#define _VIRT_REGISTERS_H_

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
 * GICv2
 * ====================================================================== */

#define VIRT_GICD_BASE		0x08000000UL	/* distributor */
#define VIRT_GICD_SIZE		0x10000UL
#define VIRT_GICC_BASE		0x08010000UL	/* CPU interface */
#define VIRT_GICC_SIZE		0x10000UL

/* Distributor register offsets */
#define GICD_CTLR		0x000
#define GICD_TYPER		0x004
#define GICD_IIDR		0x008
#define GICD_IGROUPR(n)		(0x080 + 4 * (n))
#define GICD_ISENABLER(n)	(0x100 + 4 * (n))
#define GICD_ICENABLER(n)	(0x180 + 4 * (n))
#define GICD_ISPENDR(n)		(0x200 + 4 * (n))
#define GICD_ICPENDR(n)		(0x280 + 4 * (n))
#define GICD_IPRIORITYR(n)	(0x400 + 4 * (n))
#define GICD_ITARGETSR(n)	(0x800 + 4 * (n))
#define GICD_ICFGR(n)		(0xc00 + 4 * (n))

#define GICD_CTLR_ENABLE	(1 << 0)

/* GICD_TYPER: ITLinesNumber is bits [4:0]; supported INTIDs = 32 * (N + 1) */
#define GICD_TYPER_ITLINES(v)	((((v) & 0x1f) + 1) * 32)

/* CPU interface register offsets */
#define GICC_CTLR		0x000
#define GICC_PMR		0x004	/* priority mask */
#define GICC_BPR		0x008	/* binary point */
#define GICC_IAR		0x00c	/* interrupt acknowledge */
#define GICC_EOIR		0x010	/* end of interrupt */

#define GICC_CTLR_ENABLE	(1 << 0)

/* GICC_IAR: INTID is bits [9:0]; 1023 means there was nothing to take */
#define GICC_IAR_INTID_MASK	0x3ff
#define GIC_SPURIOUS_INTID	1023

/* INTID ranges */
#define GIC_SGI_BASE		0
#define GIC_PPI_BASE		16
#define GIC_SPI_BASE		32
#define GIC_MAX_INTID		256

/* ======================================================================
 * ARM generic timer
 *
 * No registers to map: it is reached through system registers. The only
 * board-specific part is which PPI it raises.
 * ====================================================================== */

#define GIC_PPI_TIMER_HYP	26	/* device tree <1 10> */
#define GIC_PPI_TIMER_VIRT	27	/* <1 11> */
#define GIC_PPI_TIMER_SEC	29	/* <1 13> */
#define GIC_PPI_TIMER_NONSEC	30	/* <1 14> - what EL1 uses */

#endif /* _VIRT_REGISTERS_H_ */
