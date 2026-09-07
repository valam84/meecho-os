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

/* --------------------------------------------------------- the PHY */

/* Clause 22, the registers every PHY has. */
#define MII_BMCR			0x00
#define MII_BMCR_RESET			(1u << 15)
#define MII_BMCR_ANEG_RESTART		(1u << 9)
#define MII_BMCR_ANEG_ENABLE		(1u << 12)

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

#endif /* _DWMAC_REG_H */
