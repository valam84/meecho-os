/*
 * Register definitions for Broadcom BCM2711 (Raspberry Pi 4 / Compute Module 4).
 *
 * All addresses here are ARM PHYSICAL addresses, already translated from the
 * "bus" addresses used in the Broadcom datasheet and in the device tree.
 * The SoC ranges (arch/arm/boot/dts/broadcom/bcm2711.dtsi) are:
 *
 *	bus 0x7e000000 -> phys 0xfe000000, size 0x01800000
 *	bus 0x7c000000 -> phys 0xfc000000, size 0x02000000
 *	bus 0x40000000 -> phys 0xff800000, size 0x00800000
 *
 * Interrupt numbers are GIC INTIDs. The device tree lists them as
 * "GIC_SPI n", and INTID = n + 32.
 */

#ifndef _BCM2711_REGISTERS_H
#define _BCM2711_REGISTERS_H

/* ======================================================================
 * PL011 UART0 -- primary debug console, GPIO14/15
 * ====================================================================== */

#define BCM2711_UART0_BASE	0xfe201000
#define BCM2711_UART0_SIZE	0x1000
#define BCM2711_UART0_IRQ	153	/* GIC_SPI 121 */

/* PL011 register offsets */
#define PL011_DR		0x000	/* data */
#define PL011_RSR_ECR		0x004	/* receive status / error clear */
#define PL011_FR		0x018	/* flag */
#define PL011_IBRD		0x024	/* integer baud rate divisor */
#define PL011_FBRD		0x028	/* fractional baud rate divisor */
#define PL011_LCRH		0x02c	/* line control */
#define PL011_CR		0x030	/* control */
#define PL011_IFLS		0x034	/* interrupt FIFO level select */
#define PL011_IMSC		0x038	/* interrupt mask set/clear */
#define PL011_RIS		0x03c	/* raw interrupt status */
#define PL011_MIS		0x040	/* masked interrupt status */
#define PL011_ICR		0x044	/* interrupt clear */

/* PL011_FR bits */
#define PL011_FR_CTS		(1 << 0)
#define PL011_FR_BUSY		(1 << 3)
#define PL011_FR_RXFE		(1 << 4)	/* receive FIFO empty */
#define PL011_FR_TXFF		(1 << 5)	/* transmit FIFO full */
#define PL011_FR_RXFF		(1 << 6)
#define PL011_FR_TXFE		(1 << 7)	/* transmit FIFO empty */

/* PL011_LCRH bits */
#define PL011_LCRH_BRK		(1 << 0)
#define PL011_LCRH_PEN		(1 << 1)
#define PL011_LCRH_EPS		(1 << 2)
#define PL011_LCRH_STP2		(1 << 3)
#define PL011_LCRH_FEN		(1 << 4)	/* enable FIFOs */
#define PL011_LCRH_WLEN_8	(3 << 5)

/* PL011_CR bits */
#define PL011_CR_UARTEN		(1 << 0)
#define PL011_CR_TXE		(1 << 8)
#define PL011_CR_RXE		(1 << 9)

/* All interrupts, for masking/clearing */
#define PL011_INT_ALL		0x7ff

/*
 * The UART reference clock on the Pi 4 is set by the firmware and firmware
 * revisions differ. Pin it explicitly in config.txt:
 *
 *	init_uart_clock=48000000
 *
 * VERIFY on real hardware; a wrong value here produces garbage on the wire.
 * Under QEMU the PL011 model ignores the divisors entirely.
 */
#define BCM2711_UART0_CLOCK	48000000

/* ======================================================================
 * GIC-400 (ARM GICv2)
 * ====================================================================== */

#define BCM2711_GICD_BASE	0xff841000	/* distributor */
#define BCM2711_GICD_SIZE	0x1000
#define BCM2711_GICC_BASE	0xff842000	/* CPU interface */
#define BCM2711_GICC_SIZE	0x2000

/* Distributor register offsets */
#define GICD_CTLR		0x000
#define GICD_TYPER		0x004
#define GICD_IIDR		0x008
#define GICD_IGROUPR(n)		(0x080 + 4 * (n))
#define GICD_ISENABLER(n)	(0x100 + 4 * (n))
#define GICD_ICENABLER(n)	(0x180 + 4 * (n))
#define GICD_ISPENDR(n)		(0x200 + 4 * (n))
#define GICD_ICPENDR(n)		(0x280 + 4 * (n))
#define GICD_ISACTIVER(n)	(0x300 + 4 * (n))
#define GICD_ICACTIVER(n)	(0x380 + 4 * (n))
#define GICD_IPRIORITYR(n)	(0x400 + 4 * (n))
#define GICD_ITARGETSR(n)	(0x800 + 4 * (n))
#define GICD_ICFGR(n)		(0xc00 + 4 * (n))
#define GICD_SGIR		0xf00

#define GICD_CTLR_ENABLE	(1 << 0)

/* GICD_TYPER: ITLinesNumber is bits [4:0]; supported INTIDs = 32 * (N + 1) */
#define GICD_TYPER_ITLINES(v)	(((v) & 0x1f) + 1)

/* CPU interface register offsets */
#define GICC_CTLR		0x000
#define GICC_PMR		0x004	/* priority mask */
#define GICC_BPR		0x008	/* binary point */
#define GICC_IAR		0x00c	/* interrupt acknowledge */
#define GICC_EOIR		0x010	/* end of interrupt */
#define GICC_RPR		0x014
#define GICC_HPPIR		0x018

#define GICC_CTLR_ENABLE	(1 << 0)

/* GICC_IAR: INTID is bits [9:0]; 1023 means spurious */
#define GICC_IAR_INTID_MASK	0x3ff
#define GIC_SPURIOUS_INTID	1023

/* INTID ranges */
#define GIC_SGI_BASE		0
#define GIC_PPI_BASE		16
#define GIC_SPI_BASE		32
#define GIC_MAX_INTID		256	/* BCM2711 has 192+; round up */

/* ARM generic timer PPIs (INTIDs, not SPI-relative) */
#define GIC_PPI_TIMER_HYP	26
#define GIC_PPI_TIMER_VIRT	27
#define GIC_PPI_TIMER_SEC	29
#define GIC_PPI_TIMER_NONSEC	30

/* ======================================================================
 * System Timer -- free-running 1 MHz 64-bit counter + 4 compare channels
 *
 * IMPORTANT: channels 0 and 2 are used by the VideoCore firmware.
 * Only channels 1 and 3 are available to us.
 * ====================================================================== */

#define BCM2711_SYSTIMER_BASE	0xfe003000
#define BCM2711_SYSTIMER_SIZE	0x1000
#define BCM2711_SYSTIMER_HZ	1000000	/* fixed 1 MHz */

#define SYSTIMER_CS		0x00	/* control/status: match flags M0..M3 */
#define SYSTIMER_CLO		0x04	/* counter, low 32 bits */
#define SYSTIMER_CHI		0x08	/* counter, high 32 bits */
#define SYSTIMER_C0		0x0c
#define SYSTIMER_C1		0x10
#define SYSTIMER_C2		0x14
#define SYSTIMER_C3		0x18

#define SYSTIMER_CS_M(n)	(1 << (n))

/* GIC_SPI 64..67 -> INTID 96..99, one per compare channel */
#define BCM2711_SYSTIMER_IRQ(n)	(96 + (n))

/* The channel we use for the periodic system tick */
#define BCM2711_TICK_CHANNEL	3
#define BCM2711_TICK_IRQ	BCM2711_SYSTIMER_IRQ(BCM2711_TICK_CHANNEL)

/* ======================================================================
 * Power management / watchdog -- used for reset and poweroff
 * ====================================================================== */

#define BCM2711_PM_BASE		0xfe100000
#define BCM2711_PM_SIZE		0x1000

#define PM_RSTC			0x1c
#define PM_RSTS			0x20
#define PM_WDOG			0x24

#define PM_PASSWORD		0x5a000000
#define PM_WDOG_MASK		0x00000fff
#define PM_RSTC_WRCFG_MASK	0x00000030
#define PM_RSTC_WRCFG_FULL_RESET 0x00000020
#define PM_RSTC_RESET		0x00000102

/*
 * Poweroff on the Pi is done by asking the firmware to halt: set the
 * partition field in PM_RSTS to 63 and then trigger a reset. The firmware
 * reads that value and stops instead of rebooting.
 */
#define PM_RSTS_PARTITION_MASK	0x00000aaa
#define PM_RSTS_HALT_PARTITION	0x00000555	/* partition 63, interleaved */

/* ======================================================================
 * GPIO -- needed for padconf (function select on the UART pins)
 * ====================================================================== */

#define BCM2711_GPIO_BASE	0xfe200000
#define BCM2711_GPIO_SIZE	0x1000

#define GPIO_GPFSEL(n)		(0x00 + 4 * (n))	/* n = 0..5 */
#define GPIO_GPSET(n)		(0x1c + 4 * (n))
#define GPIO_GPCLR(n)		(0x28 + 4 * (n))
#define GPIO_GPLEV(n)		(0x34 + 4 * (n))

/*
 * BCM2711 replaces the bcm2835 GPPUD/GPPUDCLK sequence with direct
 * pull-up/pull-down control registers, 16 pins per register.
 */
#define GPIO_PUP_PDN_CNTRL(n)	(0xe4 + 4 * (n))	/* n = 0..3 */

#define GPIO_PUD_NONE		0
#define GPIO_PUD_UP		1
#define GPIO_PUD_DOWN		2

/* Alternate functions, as encoded in GPFSEL (3 bits per pin) */
#define GPIO_FSEL_INPUT		0
#define GPIO_FSEL_OUTPUT	1
#define GPIO_FSEL_ALT0		4
#define GPIO_FSEL_ALT1		5
#define GPIO_FSEL_ALT2		6
#define GPIO_FSEL_ALT3		7
#define GPIO_FSEL_ALT4		3
#define GPIO_FSEL_ALT5		2

/* UART0 (PL011) is ALT0 on GPIO14 (TXD) and GPIO15 (RXD) */
#define BCM2711_UART0_TXD_PIN	14
#define BCM2711_UART0_RXD_PIN	15

/* ======================================================================
 * Devices for later phases -- addresses recorded now so they are not
 * looked up twice.
 * ====================================================================== */

#define BCM2711_EMMC2_BASE	0xfe340000	/* SD/eMMC, SDHCI-compatible */
#define BCM2711_EMMC2_IRQ	158		/* GIC_SPI 126 */

#define BCM2711_GENET_BASE	0xfd580000	/* Ethernet */
#define BCM2711_GENET_IRQ0	189		/* GIC_SPI 157 */
#define BCM2711_GENET_IRQ1	190		/* GIC_SPI 158 */

#define BCM2711_ARM_LOCAL_BASE	0xff800000	/* ARM local peripherals */

#endif /* _BCM2711_REGISTERS_H */
