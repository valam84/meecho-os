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
 * GICv2 -- the interrupt controller
 *
 * Register offsets only. Unlike the UART above, the GIC's base addresses are
 * read out of the device tree rather than written here, and the asymmetry is
 * deliberate.
 *
 * The console has to come up before anything else, because a failure to parse
 * the device tree has to be reportable - so bsp_ser_init() runs as the first
 * statement of pre_init(), before the kernel has even recorded where the tree
 * is. It has no choice but to know its own address. The GIC is needed later,
 * once the tree has been read, so it can be asked instead. That is what lets
 * the same source answer for a GIC-400 on a BCM2711, which sits at
 * 0xff841000 and 0xff842000 and is otherwise the same GICv2.
 * ====================================================================== */

/* Distributor: what is enabled, at what priority, and where it goes. */
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

/* CPU interface: acknowledge and retire. */
#define GICC_CTLR		0x000
#define GICC_PMR		0x004	/* priority mask */
#define GICC_BPR		0x008	/* binary point */
#define GICC_IAR		0x00c	/* interrupt acknowledge */
#define GICC_EOIR		0x010	/* end of interrupt */

#define GICC_CTLR_ENABLE	(1 << 0)

/* GICC_IAR: INTID is bits [9:0]; 1023 means there was nothing to take */
#define GICC_IAR_INTID_MASK	0x3ff
#define GIC_SPURIOUS_INTID	1023

/* Where the three classes of interrupt ID begin. */
#define GIC_SGI_BASE		0	/* software generated */
#define GIC_PPI_BASE		16	/* per-CPU, where the timer arrives */
#define GIC_SPI_BASE		32	/* shared peripherals */

#endif /* _VIRT_REGISTERS_H_ */
