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

#endif /* _SDMMC_SDHCI_H */
