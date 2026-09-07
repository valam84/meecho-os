#ifndef _DWMAC_REG_H
#define _DWMAC_REG_H

/*
 * Registers of the Synopsys DWMAC 4/5 and of the Rockchip glue around it.
 *
 * The numbers here are facts about the hardware, taken from the reference
 * that the board itself points at: the Linux driver of the very version
 * running on the vendor system (6.1.115), read before a line of this driver
 * was written.  What the board reports and where each number came from is
 * written up in port/cb2-gmac/registers.md; that file is the one to read
 * first, and it explains the three things that do not follow from the
 * Synopsys specification at all - the pin route, the RGMII delay lines, and
 * the fact that link speed here is a clock rate rather than a register bit.
 */

/* ------------------------------------------------------------ the MAC */

/*
 * Version, and the reason it is the first thing this driver reads: the low
 * byte is the Synopsys ID and the byte above it the vendor's user ID, and
 * the board says they are 0x51 and 0x30.  A read that answers 0x3051 proves
 * in one number that the registers are mapped, the bus clock is running and
 * the block is out of reset - before anything harder is attempted.
 */
#define DWMAC_VERSION			0x0110
#define DWMAC_VERSION_SNPS(v)		((v) & 0xff)
#define DWMAC_VERSION_USER(v)		(((v) >> 8) & 0xff)
#define DWMAC_SNPS_ID_5_10		0x51

#define DWMAC_MAC_CONFIG		0x0000
#define DWMAC_MAC_CONFIG_RE		(1u << 0)	/* receiver enable */
#define DWMAC_MAC_CONFIG_TE		(1u << 1)	/* transmitter */
#define DWMAC_MAC_CONFIG_DM		(1u << 13)	/* full duplex */
#define DWMAC_MAC_CONFIG_FES		(1u << 14)	/* 100, not 10 */
#define DWMAC_MAC_CONFIG_PS		(1u << 15)	/* MII, not GMII */

#define DWMAC_MAC_ADDR_HIGH(n)		(0x0300 + (n) * 8)
#define DWMAC_MAC_ADDR_LOW(n)		(0x0304 + (n) * 8)
#define DWMAC_MAC_ADDR_HIGH_AE		(1u << 31)	/* entry enable */

/*
 * MDIO.  One address register that both starts the transfer and reports it
 * finished, and one data register.
 */
#define DWMAC_MDIO_ADDR			0x0200
#define DWMAC_MDIO_DATA			0x0204

#define DWMAC_MDIO_ADDR_GB		(1u << 0)	/* busy */
#define DWMAC_MDIO_ADDR_C45E		(1u << 1)
#define DWMAC_MDIO_ADDR_GOC_SHIFT	2
#define DWMAC_MDIO_ADDR_GOC_WRITE	(1u << DWMAC_MDIO_ADDR_GOC_SHIFT)
#define DWMAC_MDIO_ADDR_GOC_READ	(3u << DWMAC_MDIO_ADDR_GOC_SHIFT)
#define DWMAC_MDIO_ADDR_CR_SHIFT	8		/* MDC divider */
#define DWMAC_MDIO_ADDR_CR_MASK		(0xfu << 8)
#define DWMAC_MDIO_ADDR_RDA_SHIFT	16		/* PHY register */
#define DWMAC_MDIO_ADDR_RDA_MASK	(0x1fu << 16)
#define DWMAC_MDIO_ADDR_PA_SHIFT	21		/* PHY address */
#define DWMAC_MDIO_ADDR_PA_MASK		(0x1fu << 21)

#define DWMAC_MDIO_DATA_MASK		0xffff

/*
 * The MDC divider.  The field names a range for the clock feeding the
 * block, and the hardware divides by a fixed factor per encoding; asking
 * for a divider meant for a faster clock than the one actually present only
 * makes MDC slower, which is always legal, while the other way round is
 * not.  0x5 is divide-by-124, which keeps MDC under the 2.5 MHz the
 * standard allows for any plausible rate of this bus - and the rate of this
 * bus is one thing the board has not been asked yet.
 */
#define DWMAC_MDIO_CR_SAFE		0x5

/* The software reset lives with the DMA, and clears itself when done. */
#define DWMAC_DMA_BUS_MODE		0x1000
#define DWMAC_DMA_BUS_MODE_SWR		(1u << 0)

/* --------------------------------------------------- the Rockchip glue */

/*
 * Every register below is hiword-masked: the upper sixteen bits say which
 * of the lower sixteen a write may change, and a read gives back only the
 * lower ones.  Same convention as the CRU registers the sdmmc driver
 * already writes.
 */
#define RK_HIWORD(val, mask, shift)					\
	(((uint32_t)(val) << (shift)) | ((uint32_t)(mask) << ((shift) + 16)))

/* GRF: the pin multiplexer of a bank, four bits per pin. */
#define RK3568_GRF_IOMUX_GPIO3		0x0040
#define RK3568_GRF_IOMUX_BANK_STRIDE	0x0020

/*
 * GRF: which of the two pin sets, m0 or m1, GMAC1 is wired to.  Nothing in
 * the controller says this and nothing in the device tree says it directly
 * either - the tree says it by the name of the pin group it asks for.  Set
 * it wrong and fifteen correctly multiplexed pins lead nowhere.
 */
#define RK3568_GRF_IOFUNC_SEL0		0x0300
#define RK3568_GMAC1_IOMUX_SEL_BIT	8

/* GRF: the delay lines and the interface mode, one pair per controller. */
#define RK3568_GRF_GMAC0_CON0		0x0380
#define RK3568_GRF_GMAC0_CON1		0x0384
#define RK3568_GRF_GMAC1_CON0		0x0388
#define RK3568_GRF_GMAC1_CON1		0x038c

#define RK3568_GMAC_CON0_RX_DL_SHIFT	8
#define RK3568_GMAC_CON0_TX_DL_SHIFT	0
#define RK3568_GMAC_CON0_DL_MASK	0x7f

#define RK3568_GMAC_CON1_TXCLK_DLY	0
#define RK3568_GMAC_CON1_RXCLK_DLY	1
#define RK3568_GMAC_CON1_FLOW_CTRL	3
#define RK3568_GMAC_CON1_INTF_SEL_SHIFT	4
#define RK3568_GMAC_CON1_INTF_SEL_MASK	0x7
#define RK3568_GMAC_CON1_INTF_RGMII	0x1
#define RK3568_GMAC_CON1_INTF_RMII	0x4

/*
 * CRU.  The offsets are the ones sdmmc already uses; what is new here is
 * which registers matter for GMAC1, and that all of them are one register.
 */
#define RK3568_CRU_CLKSEL_CON(n)	(0x0100 + (n) * 4)
#define RK3568_CRU_CLKGATE_CON(n)	(0x0300 + (n) * 4)
#define RK3568_CRU_SOFTRST_CON(n)	(0x0400 + (n) * 4)
#define RK3568_CRU_RESETS_PER_REG	16

/* Everything about GMAC1's clocks is in CLKSEL_CON(33). */
#define RK3568_CRU_GMAC1_CLKSEL		RK3568_CRU_CLKSEL_CON(33)
#define RK3568_GMAC1_RXTX_SRC_SHIFT	0	/* 0 RGMII, 1 RMII, 2 XPCS */
#define RK3568_GMAC1_RXTX_SRC_MASK	0x3
#define RK3568_GMAC1_RXTX_SRC_RGMII	0
#define RK3568_GMAC1_CLK_FROM_PHY_BIT	2	/* 1: the PHY drives us */
#define RK3568_GMAC1_SPEED_SHIFT	4
#define RK3568_GMAC1_SPEED_MASK		0x3
#define RK3568_GMAC1_SPEED_1000		0	/* 125 MHz */
#define RK3568_GMAC1_SPEED_10		2	/* 2.5 MHz */
#define RK3568_GMAC1_SPEED_100		3	/* 25 MHz */

/* And the gates, in CLKGATE_CON(17).  A bit set means the clock is off. */
#define RK3568_CRU_GMAC1_CLKGATE	RK3568_CRU_CLKGATE_CON(17)
#define RK3568_GMAC1_GATE_PTP_REF	(1u << 2)
#define RK3568_GMAC1_GATE_ACLK		(1u << 3)
#define RK3568_GMAC1_GATE_PCLK		(1u << 4)
#define RK3568_GMAC1_GATE_MAC1_2TOP	(1u << 5)
#define RK3568_GMAC1_GATE_REFOUT	(1u << 10)

/* ------------------------------------------------------- the GPIO bank */

/*
 * Version 2 of the Rockchip GPIO block, which is what the RK356x has: the
 * data and direction registers are split into a low and a high half of
 * sixteen pins each, and both halves are hiword-masked.
 */
#define RK_GPIO_SWPORT_DR_L		0x0000
#define RK_GPIO_SWPORT_DR_H		0x0004
#define RK_GPIO_SWPORT_DDR_L		0x0008
#define RK_GPIO_SWPORT_DDR_H		0x000c
#define RK_GPIO_VERSION_ID		0x0078
#define RK_GPIO_VERSION_V2		0x01000c2b

/* ------------------------------------------------- rings and channels */

/*
 * One transmit and one receive channel, which is what the board's device
 * tree asks for and all this driver uses.
 */
#define DWMAC_DMA_CHAN			0
#define DWMAC_DMA_CHAN_BASE(c)		(0x1100 + (c) * 0x80)

#define DWMAC_DMA_CH_CONTROL(c)		(DWMAC_DMA_CHAN_BASE(c) + 0x00)
#define DWMAC_DMA_CH_TX_CONTROL(c)	(DWMAC_DMA_CHAN_BASE(c) + 0x04)
#define DWMAC_DMA_CH_RX_CONTROL(c)	(DWMAC_DMA_CHAN_BASE(c) + 0x08)
#define DWMAC_DMA_CH_TXDESC_HI(c)	(DWMAC_DMA_CHAN_BASE(c) + 0x10)
#define DWMAC_DMA_CH_TXDESC_LO(c)	(DWMAC_DMA_CHAN_BASE(c) + 0x14)
#define DWMAC_DMA_CH_RXDESC_HI(c)	(DWMAC_DMA_CHAN_BASE(c) + 0x18)
#define DWMAC_DMA_CH_RXDESC_LO(c)	(DWMAC_DMA_CHAN_BASE(c) + 0x1c)
#define DWMAC_DMA_CH_TXTAIL(c)		(DWMAC_DMA_CHAN_BASE(c) + 0x20)
#define DWMAC_DMA_CH_RXTAIL(c)		(DWMAC_DMA_CHAN_BASE(c) + 0x28)
#define DWMAC_DMA_CH_TXLEN(c)		(DWMAC_DMA_CHAN_BASE(c) + 0x2c)
#define DWMAC_DMA_CH_RXLEN(c)		(DWMAC_DMA_CHAN_BASE(c) + 0x30)
#define DWMAC_DMA_CH_INTR_ENA(c)	(DWMAC_DMA_CHAN_BASE(c) + 0x34)
#define DWMAC_DMA_CH_CUR_TXDESC(c)	(DWMAC_DMA_CHAN_BASE(c) + 0x44)
#define DWMAC_DMA_CH_CUR_RXDESC(c)	(DWMAC_DMA_CHAN_BASE(c) + 0x4c)
#define DWMAC_DMA_CH_STATUS(c)		(DWMAC_DMA_CHAN_BASE(c) + 0x60)

/*
 * The burst length: how many beats the DMA asks the bus for at a time.
 * It has no useful reset value and every driver programs it - eight is what
 * both Linux and NetBSD use when the device tree says nothing, and this
 * board's tree says nothing.  PBLX8 multiplies it by eight, which is also
 * what both of them do.
 */
#define DWMAC_DMA_CH_CONTROL_PBLX8	(1u << 16)
#define DWMAC_DMA_CH_PBL_SHIFT		16
#define DWMAC_DMA_CH_PBL_MASK		(0x3fu << 16)
#define DWMAC_DMA_PBL_DEFAULT		8

#define DWMAC_DMA_CH_TX_CONTROL_ST	(1u << 0)
#define DWMAC_DMA_CH_TX_CONTROL_OSP	(1u << 4)
#define DWMAC_DMA_CH_RX_CONTROL_SR	(1u << 0)
#define DWMAC_DMA_CH_RX_CONTROL_RBSZ_SHIFT 1
#define DWMAC_DMA_CH_RX_CONTROL_RBSZ_MASK  (0x3fffu << 1)

#define DWMAC_DMA_CH_STATUS_TI		(1u << 0)
#define DWMAC_DMA_CH_STATUS_TPS		(1u << 1)
#define DWMAC_DMA_CH_STATUS_TBU		(1u << 2)
#define DWMAC_DMA_CH_STATUS_RI		(1u << 6)
#define DWMAC_DMA_CH_STATUS_RBU		(1u << 7)
#define DWMAC_DMA_CH_STATUS_RPS		(1u << 8)
#define DWMAC_DMA_CH_STATUS_FBE		(1u << 12)
#define DWMAC_DMA_CH_STATUS_AIS		(1u << 14)
#define DWMAC_DMA_CH_STATUS_NIS		(1u << 15)

/*
 * The interrupt enables moved between core 4.00 and core 4.10, and this is
 * a 5.10: the normal and abnormal summary enables are bits 15 and 14 here,
 * not 16 and 15.  Writing the older pair on this core enables the wrong two
 * things quietly.
 */
#define DWMAC_DMA_CH_INTR_TIE		(1u << 0)
#define DWMAC_DMA_CH_INTR_RIE		(1u << 6)
#define DWMAC_DMA_CH_INTR_FBE		(1u << 12)
#define DWMAC_DMA_CH_INTR_AIE		(1u << 14)
#define DWMAC_DMA_CH_INTR_NIE		(1u << 15)

#define DWMAC_DMA_SYS_BUS_MODE		0x1004
#define DWMAC_DMA_SYS_BUS_MB		(1u << 14)
#define DWMAC_DMA_SYS_BUS_AAL		(1u << 12)
#define DWMAC_DMA_SYS_BUS_BLEN16	(1u << 3)
#define DWMAC_DMA_SYS_BUS_BLEN8		(1u << 2)
#define DWMAC_DMA_SYS_BUS_BLEN4		(1u << 1)

/* The MTL queues that sit between the MAC and the DMA. */
#define DWMAC_MTL_CHAN_BASE(q)		(0x0d00 + (q) * 0x40)
#define DWMAC_MTL_TXQ_OP_MODE(q)	(DWMAC_MTL_CHAN_BASE(q) + 0x00)
#define DWMAC_MTL_TXQ_QUANTUM(q)	(DWMAC_MTL_CHAN_BASE(q) + 0x18)
#define DWMAC_MTL_RXQ_OP_MODE(q)	(DWMAC_MTL_CHAN_BASE(q) + 0x30)

#define DWMAC_MTL_OP_MODE_TSF		(1u << 1)	/* store and forward */
#define DWMAC_MTL_OP_MODE_TXQEN_SHIFT	2
#define DWMAC_MTL_OP_MODE_TXQEN_ON	2
#define DWMAC_MTL_OP_MODE_RSF		(1u << 5)
#define DWMAC_MTL_OP_MODE_TQS_SHIFT	16
#define DWMAC_MTL_OP_MODE_TQS_MASK	(0x1ffu << 16)
#define DWMAC_MTL_OP_MODE_RQS_SHIFT	20
#define DWMAC_MTL_OP_MODE_RQS_MASK	(0x3ffu << 20)

#define DWMAC_MAC_RXQ_CTRL0		0x00a0
#define DWMAC_MAC_RXQ_CTRL0_Q0_MASK	0x3
#define DWMAC_MAC_RXQ_CTRL0_Q0_DCB	0x2

#define DWMAC_MAC_PACKET_FILTER		0x0008
#define DWMAC_MAC_PACKET_FILTER_PM	(1u << 4)	/* all multicast */
#define DWMAC_MAC_PACKET_FILTER_PR	(1u << 0)	/* promiscuous */

/*
 * The FIFO sizes are reported, not assumed: the field is the log2 of the
 * size in units of 128 bytes, and the queue-size fields want (size / 256) - 1.
 */
#define DWMAC_MAC_HW_FEATURE1		0x0120
#define DWMAC_HW_FEATURE1_RXFIFO_SHIFT	0
#define DWMAC_HW_FEATURE1_TXFIFO_SHIFT	6
#define DWMAC_HW_FEATURE1_FIFO_MASK	0x1f

/*
 * A descriptor: four words, and which word means what depends on whether
 * the CPU is writing it or reading back what the device wrote.
 */
struct dwmac_desc {
	uint32_t des0;
	uint32_t des1;
	uint32_t des2;
	uint32_t des3;
};

/* Transmit, as the CPU writes it. */
#define TDES2_BUF1_LEN_MASK		0x3fff
#define TDES2_IOC			(1u << 31)	/* interrupt when done */
#define TDES3_PACKET_LEN_MASK		0x7fff
#define TDES3_LAST			(1u << 28)
#define TDES3_FIRST			(1u << 29)
#define TDES3_OWN			(1u << 31)

/* Transmit, as the device writes it back. */
#define TDES3_WB_UNDERFLOW		(1u << 2)
#define TDES3_WB_EXCESS_COLL		(1u << 8)
#define TDES3_WB_LATE_COLL		(1u << 9)
#define TDES3_WB_NO_CARRIER		(1u << 10)
#define TDES3_WB_LOSS_CARRIER		(1u << 11)
#define TDES3_WB_PACKET_FLUSHED		(1u << 13)
#define TDES3_WB_ERROR_SUMMARY		(1u << 15)

/* Receive: what the CPU writes, and what comes back. */
#define RDES3_BUF1_VALID		(1u << 24)
#define RDES3_IOC			(1u << 30)
#define RDES3_OWN			(1u << 31)
#define RDES3_PACKET_LEN_MASK		0x7fff
#define RDES3_ERROR_SUMMARY		(1u << 15)
#define RDES3_LAST			(1u << 28)
#define RDES3_FIRST			(1u << 29)

/*
 * The MAC's own counters.  What the driver thinks it did is one thing and
 * what the hardware says it did is another, and when the two disagree the
 * hardware is right.  These are the two that answer "did a frame actually
 * leave" and "did one actually arrive", which no amount of looking at
 * descriptors can.
 */
#define DWMAC_MMC_BASE			0x0700
#define DWMAC_MMC_TX_FRAMES_GB		(DWMAC_MMC_BASE + 0x18)
#define DWMAC_MMC_TX_FRAMES_G		(DWMAC_MMC_BASE + 0x68)
#define DWMAC_MMC_RX_FRAMES_GB		(DWMAC_MMC_BASE + 0x80)

/* --------------------------------------------------------- the PHY */

/* Clause 22, the registers every PHY has. */
#define MII_BMCR			0x00
#define MII_BMCR_RESET			(1u << 15)
#define MII_BMCR_ANEG_RESTART		(1u << 9)
#define MII_BMCR_ANEG_ENABLE		(1u << 12)
#define MII_BMCR_LOOPBACK		(1u << 14)
#define MII_BMCR_SPEED_100		(1u << 13)
#define MII_BMCR_FULL_DUPLEX		(1u << 8)

/* The YT8531's own idea of the link: speed, duplex, and whether it is up. */
#define YT_SPECIFIC_STATUS		0x11

#define MII_BMSR			0x01
#define MII_BMSR_LINK			(1u << 2)
#define MII_BMSR_ANEG_DONE		(1u << 5)

#define MII_PHYID1			0x02
#define MII_PHYID2			0x03

#define MII_ADVERTISE			0x04
#define MII_LPA				0x05
#define MII_LPA_100FD			(1u << 8)
#define MII_LPA_100HD			(1u << 7)
#define MII_LPA_10FD			(1u << 6)
#define MII_LPA_10HD			(1u << 5)

#define MII_CTRL1000			0x09
#define MII_STAT1000			0x0a
#define MII_STAT1000_LP_1000FD		(1u << 11)
#define MII_STAT1000_LP_1000HD		(1u << 10)

/*
 * The PHY this board has: Motorcomm YT8531, at address 0 of the
 * controller's own MDIO bus.  Its identifier is read from the board rather
 * than assumed, and checking it is the point - a driver that has multiplexed
 * the wrong pins reads 0xffff or 0x0000 here, not a plausible number.
 */
#define YT8531_PHY_ID			0x4f51e91b
#define YT8531_PHY_ID_MASK		0xffffffff

/*
 * The one thing in this driver that belongs to a particular PHY.
 *
 * The YT8531's extended registers are reached through a page window rather
 * than directly: write the number of the register to 0x1e, then read or
 * write its contents at 0x1f.
 *
 * What goes into them is not taken from a datasheet - Motorcomm does not
 * publish one - but from the system that works on this board: the vendor
 * kernel (6.1, bigtreetech/linux-rockchip) with its motorcomm.c, and the
 * registers read back from the PHY while that kernel had the link up.  The
 * values below are that snapshot.  Where a name is known it is given; where
 * only the number is known, the number is what there is.
 */
#define YT_PAGE_SELECT			0x1e
#define YT_PAGE_DATA			0x1f

/* Chip configuration.  Bit 8 enables the PHY's receive clock delay. */
#define YT_EXT_CHIP_CONFIG		0xa001
#define YT_CHIP_CONFIG_RXC_DLY_EN	(1u << 8)

/* RGMII delay selectors: receive [13:10], 100M transmit [7:4], 1G [3:0]. */
#define YT_EXT_RGMII_CONFIG1		0xa003

/*
 * The analogue side.  0x57 holds the bandgap reference voltage for the 100M
 * transmitter in bits [11:8]; the vendor driver moves it from the reset
 * value 9 to 7 ("Change 100M default BGS voltage from 0x294c to 0x274c")
 * and does not say why.  0xa010 is the drive strength of RXC, PHY_CLK_OUT
 * and RXD; 0xa012 turns the 125 MHz clock output on - on this board that
 * clock is what the SoC runs its transmit side from.
 */
#define YT_EXT_BGS_100M			0x57
#define YT_BGS_100M_MASK		(0xfu << 8)
#define YT_BGS_100M_VENDOR		(7u << 8)
#define YT_EXT_DRIVE_STRENGTH		0xa010
#define YT_DRIVE_STRENGTH_VENDOR	0xdbcf
#define YT_EXT_CLK_OUT			0xa012
#define YT_CLK_OUT_125M			0x00d0

/* Receive clock duty cycle; the vendor driver writes these six as a block. */
#define YT_EXT_RXC_DUTY_FIRST		0xa03a
#define YT_EXT_RXC_DUTY_LAST		0xa03f
#define YT_RXC_DUTY_NODELAY		0x9696
#define YT_EXT_RXC_DUTY_CTRL0		0xa039
#define YT_RXC_DUTY_CTRL0_VENDOR	0xbf00
#define YT_EXT_RXC_DUTY_CTRL1		0xa040
#define YT_RXC_DUTY_CTRL1_VENDOR	0xffff
#define YT_EXT_RXC_DUTY_CTRL2		0xa041
#define YT_RXC_DUTY_CTRL2_VENDOR	0x00ff

/*
 * The extended registers the reference snapshot covers, and their values
 * with the vendor kernel driving the PHY at 100 Mbit/s full duplex.  The
 * driver prints the same list so that a run on the board can be laid next
 * to this table line by line.
 */
#define YT_REF_REGS \
	{ 0x000c, 0x8000 }, { 0x0027, 0xe810 }, { 0x0057, 0x274c },	\
	{ 0xa000, 0x0000 }, { 0xa001, 0x8100 }, { 0xa003, 0x00f1 },	\
	{ 0xa010, 0xdbcf }, { 0xa012, 0x00d0 }, { 0xa039, 0xbf00 },	\
	{ 0xa03a, 0x9696 }, { 0xa03f, 0x9696 }, { 0xa040, 0xffff },	\
	{ 0xa041, 0x00ff }

/*
 * The one-time-programmable memory, which is not part of this controller at
 * all: a separate block with its own node in the device tree, read once at
 * start-up for the identifier the station address is made from.  Offsets
 * and the read sequence are the vendor driver's (rockchip-otp.c); Rockchip
 * publishes no manual for this block, so what is not in that driver is not
 * known here either.
 */
#define RK3568_OTP_SBPI_CTRL		0x0020
#define RK3568_OTP_SBPI_CMD_VALID_PRE	0x0024
#define RK3568_OTP_LOCK_CTRL		0x0050
#define RK3568_OTP_USER_CTRL		0x0100
#define RK3568_OTP_USER_ADDR		0x0104
#define RK3568_OTP_USER_ENABLE		0x0108
#define RK3568_OTP_USER_QP		0x0120
#define RK3568_OTP_USER_Q		0x0124
#define RK3568_OTP_INT_STATUS		0x0304
#define RK3568_OTP_SBPI_CMD0		0x1000
#define RK3568_OTP_SBPI_CMD1		0x1004

/*
 * These registers take the same hiword mask as the CRU: the upper half
 * says which bits of the lower half a write may change.
 */
#define RK3568_OTP_USER_ADDR_MASK	0xffff0000u
#define RK3568_OTP_USE_USER		(1u << 0)
#define RK3568_OTP_USE_USER_MASK	(1u << 16)
#define RK3568_OTP_USER_FSM_ENABLE	(1u << 0)
#define RK3568_OTP_USER_FSM_ENABLE_MASK	(1u << 16)
#define RK3568_OTP_LOCK			(1u << 0)
#define RK3568_OTP_LOCK_MASK		(1u << 16)

/* Status bits, cleared by writing them back. */
#define RK3568_OTP_SBPI_DONE		(1u << 1)
#define RK3568_OTP_USER_DONE		(1u << 2)

/* The side channel that reaches the OTP macro behind the controller. */
#define RK3568_OTP_SBPI_DAP_ADDR	0x02
#define RK3568_OTP_SBPI_DAP_ADDR_SHIFT	8
#define RK3568_OTP_SBPI_DAP_ADDR_MASK	0xff000000u
#define RK3568_OTP_SBPI_CMD_VALID_MASK	0xffff0000u
#define RK3568_OTP_SBPI_DAP_CMD_WRF	0xc0
#define RK3568_OTP_SBPI_DAP_REG_ECC	0x3a
#define RK3568_OTP_SBPI_ECC_ON		0x00
#define RK3568_OTP_SBPI_ECC_OFF		0x09
#define RK3568_OTP_SBPI_ENABLE		(1u << 0)
#define RK3568_OTP_SBPI_ENABLE_MASK	(1u << 16)

#define RK3568_OTP_NBYTES		2	/* an address is a word */
#define RK3568_OTP_TIMEOUT_US		10000

/*
 * Its clocks, in two gate registers because the fourth of them belongs to
 * the OTP's analogue side and lives elsewhere in the CRU.  Named in the
 * device tree as usr, sbpi, apb and phy; the register and bit of each are
 * from clk-rk3568.c, which is the only place they are written down.
 */
#define RK3568_CRU_OTP_CLKGATE		RK3568_CRU_CLKGATE_CON(26)
#define RK3568_OTP_GATE_PCLK		(1u << 9)
#define RK3568_OTP_GATE_SBPI		(1u << 10)
#define RK3568_OTP_GATE_USR		(1u << 11)
#define RK3568_OTP_GATES		(RK3568_OTP_GATE_PCLK |		\
					 RK3568_OTP_GATE_SBPI |		\
					 RK3568_OTP_GATE_USR)

#define RK3568_CRU_OTPPHY_CLKGATE	RK3568_CRU_CLKGATE_CON(34)
#define RK3568_OTPPHY_GATE		(1u << 13)

#endif /* _DWMAC_REG_H */
