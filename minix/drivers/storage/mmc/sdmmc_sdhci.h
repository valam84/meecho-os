/*
 * What sdhcreg.h does not say.
 *
 * sdhcreg.h came from NetBSD in 2006 and describes the SD Host Controller
 * Simplified Specification up to 2.00, with a few Freescale eSDHC fields
 * mixed in. Three things this driver needs are younger than that, and one
 * is a property of the part rather than of the standard.
 */

#ifndef _SDMMC_SDHCI_H
#define _SDMMC_SDHCI_H

/*
 * Eight-bit bus, Host Control 1 bit 5, added in specification 3.00 as
 * "Extended Data Transfer Width" for embedded devices.
 *
 * Not to be confused with SDHC_ESDHC_8BIT_MODE in sdhcreg.h: that is bit 2,
 * which Freescale's eSDHC uses for the same purpose and the standard uses
 * for High Speed Enable. On a standard part, writing the eSDHC bit asks for
 * high speed timing and leaves the bus four bits wide.
 */
#define SDHC_8BIT_MODE			(1<<5)

/* Host Control 2, specification 3.00: UHS mode and 1.8V signalling. */
#define SDHC_HOST_CTL2			0x3e
#define  SDHC_CTL2_UHS_MASK		0x0007
#define  SDHC_CTL2_1_8V_SIGNAL		(1<<3)
#define  SDHC_CTL2_PRESET_VALUE_ENABLE	(1<<15)

/*
 * The base clock field of the capabilities register is six bits wide in
 * specification 1.00 and 2.00 and eight bits wide from 3.00 on, which is
 * why sdhcreg.h's SDHC_BASE_FREQ_MASK cannot be used unconditionally.
 */
#define SDHC_BASE_FREQ_MASK_V3		0xff

/* Specification version, low byte of SDHC_HOST_CTL_VERSION. */
#define SDHC_SPEC_VERS_400		0x03

/*
 * What the controller may fetch by itself, and how it is told to.
 *
 * sdhcreg.h has SDHC_DMA_SUPPORT, which is the SDMA bit and the only kind
 * of DMA specification 2.00 knew; ADMA2 and the descriptor list came with
 * 3.00, and the two more bits below with it.
 *
 * SDHC_64BIT_BUS_V3 is asked rather than assumed because it decides the
 * shape of a descriptor - eight bytes with a 32-bit address, or twelve with
 * a 64-bit one - and getting that wrong is not a slow transfer but a
 * transfer into whatever the misread half of an address points at. This
 * board answers no, and all of its RAM is below 4 GiB, so nothing is lost;
 * a part that answers yes is refused DMA here rather than driven by
 * untested code.
 */
#define SDHC_ADMA2_SUPPORT		(1<<19)
#define SDHC_64BIT_BUS_V3		(1<<28)

/* Host Control 1, DMA Select: which engine the transfer mode's DMA bit means. */
#define SDHC_DMA_SELECT_SHIFT		3
#define SDHC_DMA_SELECT_MASK		0x3
#define  SDHC_DMA_SELECT_SDMA		0
#define  SDHC_DMA_SELECT_ADMA2_32	2
#define  SDHC_DMA_SELECT_ADMA2_64	3

/*
 * The ADMA2 descriptor table's address, and where ADMA reports its own
 * failures. Error Interrupt Status bit 9 is "ADMA error"; sdhcreg.h's
 * SDHC_DMA_ERROR at bit 12 is in the vendor half of that register on a
 * modern part and is not this.
 */
#define SDHC_ADMA_ERROR_STATUS		0x54
#define SDHC_ADMA_ADDR			0x58
#define SDHC_ADMA_ADDR_HI		0x5c
#define SDHC_ADMA_ERROR			(1<<9)

/*
 * One ADMA2 descriptor with a 32-bit address: attributes, length, address.
 *
 * The length field is sixteen bits and zero means 65536, which is why a
 * descriptor covers at most that. Valid says the entry is one; End says it
 * is the last, and the controller stops there; Int would raise an interrupt
 * at this entry and is not used - the transfer's own completion is the only
 * event this driver waits for.
 */
#define ADMA2_ATTR_VALID		(1<<0)
#define ADMA2_ATTR_END			(1<<1)
#define ADMA2_ATTR_INT			(1<<2)
#define ADMA2_ATTR_ACT_NOP		(0<<4)
#define ADMA2_ATTR_ACT_TRAN		(2<<4)
#define ADMA2_ATTR_ACT_LINK		(3<<4)
#define ADMA2_MAX_LEN			65536

/*
 * The Rockchip DesignWare Cores part, "rockchip,rk3568-dwcmshc".
 *
 * It is an SDHCI controller, so everything above applies unchanged; these
 * are the two vendor registers the driver has to touch. The vendor area is
 * not at a fixed offset - the part publishes where it starts in a pointer
 * register - which is why the base is read rather than written down.
 *
 * HOST_CTRL3 bit 0 is a command-conflict check that Linux clears on every
 * rk35xx before using the controller (dwcmshc_rk35xx_init). It reports a
 * conflict when the CMD line does not read back what the controller drove,
 * which on this part fires spuriously; leaving it on turns ordinary commands
 * into errors.
 */
#define DWCMSHC_VENDOR_PTR		0xe8	/* 16-bit */
#define  DWCMSHC_VENDOR_PTR_MASK	0x0fff
#define DWCMSHC_HOST_CTRL3		0x08	/* within the vendor area */
#define DWCMSHC_EMMC_CONTROL		0x2c	/* within the vendor area */
#define  DWCMSHC_CARD_IS_EMMC		(1<<0)

/*
 * The Rockchip block at 0x800, and the one bit of it that has to be set for
 * the controller to work at all: a reset clears it, and with it clear the
 * internal clock never reaches "stable", so no card clock can be started.
 * Linux calls it "enable INTERNAL CLOCK" and writes it after every reset;
 * U-Boot's driver for the same silicon never touches it, which is why the
 * first version of this driver did not either - and hung on the board.
 * These offsets are absolute, not relative to the vendor area.
 */
#define DWCMSHC_EMMC_MISC_CON		0x81c
#define  DWCMSHC_MISC_INTCLK_EN		(1<<1)

/*
 * The delay line, which decides when the controller samples what the card
 * drives back. It lives in the same Rockchip block as MISC_CON, so a
 * software reset does not touch it either, and whatever the bootloader left
 * there stays: on this board U-Boot runs the eMMC at HS200, and a sampling
 * phase computed for 200 MHz is not one that finds a response at 400 kHz.
 * The symptom is not silence but nonsense - a 136-bit response read as all
 * ones, which is an idle bus with its pull-ups, reported as a completed
 * command.
 *
 * Below 52 MHz the answer is not to train the line but to switch it off:
 * bypass the DLL and take the sample clock straight from the source, which
 * is what Linux does for every mode this driver uses.
 */
#define DWCMSHC_EMMC_DLL_CTRL		0x800
#define  DWCMSHC_DLL_START		(1<<0)
#define  DWCMSHC_DLL_BYPASS		(1<<24)
#define DWCMSHC_EMMC_DLL_RXCLK		0x804
#define  DWCMSHC_RXCLK_ORI_GATE		(1u<<31)
#define DWCMSHC_EMMC_DLL_TXCLK		0x808
#define DWCMSHC_EMMC_DLL_STRBIN		0x80c
#define  DWCMSHC_DLL_DLYENA		(1<<27)
#define  DWCMSHC_STRBIN_DELAY_SEL	(1<<26)
#define  DWCMSHC_STRBIN_DELAY_SHIFT	16
#define  DWCMSHC_STRBIN_DELAY_DEFAULT	0x16
#define DWCMSHC_EMMC_DLL_CMDOUT		0x810

#endif /* _SDMMC_SDHCI_H */
