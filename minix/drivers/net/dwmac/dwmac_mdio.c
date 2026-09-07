/*
 * MDIO, and the PHY on the other end of it.
 *
 * MDIO is the controller's own: two registers, one of which both starts a
 * transfer and reports it finished.  The PHY is spoken to in clause 22,
 * which is the part of the standard every PHY implements, so nothing here
 * is specific to the Motorcomm part this board has - except the check that
 * it is that part, which is the point of reading its identifier at all.
 *
 * That check is the cheapest proof there is that the whole chain below is
 * right.  A PHY identifier that comes back as the number the board reports
 * says the pins are multiplexed, the pin route is set, the clocks run, the
 * controller is out of reset and MDIO works.  A driver that has any of
 * those wrong reads 0xffff or 0x0000 - never a plausible number.
 */

#include <minix/drivers.h>

#include "dwmac.h"
#include "dwmacreg.h"

/*
 * A transfer is at most a few dozen MDC periods, and MDC here is under a
 * megahertz, so tens of microseconds.  A milliseconds-long limit is
 * generous by two orders of magnitude and still bounded, which is what
 * matters: a driver that spins forever on a bus with nothing on it is a
 * system that does not boot.
 */
#define MDIO_TIMEOUT_US		10000
#define MDIO_POLL_US		10

static int
mdio_wait(void)
{
	unsigned waited;

	for (waited = 0; waited < MDIO_TIMEOUT_US; waited += MDIO_POLL_US) {
		if (!(dwmac_rd(dwmac.mac, DWMAC_MDIO_ADDR) &
		    DWMAC_MDIO_ADDR_GB))
			return OK;
		micro_delay(MDIO_POLL_US);
	}

	return EIO;
}

static uint32_t
mdio_frame(int phyaddr, int reg)
{
	return DWMAC_MDIO_ADDR_GB |
	    (((uint32_t)phyaddr << DWMAC_MDIO_ADDR_PA_SHIFT) &
	    DWMAC_MDIO_ADDR_PA_MASK) |
	    (((uint32_t)reg << DWMAC_MDIO_ADDR_RDA_SHIFT) &
	    DWMAC_MDIO_ADDR_RDA_MASK) |
	    (((uint32_t)DWMAC_MDIO_CR_SAFE << DWMAC_MDIO_ADDR_CR_SHIFT) &
	    DWMAC_MDIO_ADDR_CR_MASK);
}

int
dwmac_mdio_read(int phyaddr, int reg, uint16_t *val)
{
	int r;

	if ((r = mdio_wait()) != OK)
		return r;

	dwmac_wr(dwmac.mac, DWMAC_MDIO_DATA, 0);
	dwmac_wr(dwmac.mac, DWMAC_MDIO_ADDR,
	    mdio_frame(phyaddr, reg) | DWMAC_MDIO_ADDR_GOC_READ);

	if ((r = mdio_wait()) != OK)
		return r;

	*val = (uint16_t)(dwmac_rd(dwmac.mac, DWMAC_MDIO_DATA) &
	    DWMAC_MDIO_DATA_MASK);
	return OK;
}

int
dwmac_mdio_write(int phyaddr, int reg, uint16_t val)
{
	int r;

	if ((r = mdio_wait()) != OK)
		return r;

	dwmac_wr(dwmac.mac, DWMAC_MDIO_DATA, val);
	dwmac_wr(dwmac.mac, DWMAC_MDIO_ADDR,
	    mdio_frame(phyaddr, reg) | DWMAC_MDIO_ADDR_GOC_WRITE);

	return mdio_wait();
}

/*
 * Read the identifier of the PHY the tree says is there, and if the tree
 * said nothing, look for one.
 *
 * Looking is thirty-two reads on a bus that answers in microseconds, and it
 * is what turns "the link is down" into "there is nothing on the bus",
 * which are different problems with different causes.
 */
int
dwmac_phy_find(void)
{
	uint16_t id1, id2;
	uint32_t id;
	int addr, first, last;

	first = (dwmac.info.phy_addr >= 0) ? dwmac.info.phy_addr : 0;
	last = (dwmac.info.phy_addr >= 0) ? dwmac.info.phy_addr : 31;

	for (addr = first; addr <= last; addr++) {
		if (dwmac_mdio_read(addr, MII_PHYID1, &id1) != OK)
			continue;
		if (dwmac_mdio_read(addr, MII_PHYID2, &id2) != OK)
			continue;

		id = ((uint32_t)id1 << 16) | id2;

		/* Nothing on the bus reads as all ones or all zeroes. */
		if (id == 0 || id == 0xffffffff)
			continue;

		dwmac.info.phy_addr = addr;
		dwmac.phy_id = id;

		log_info(&dwmac_log, "phy at %d: id 0x%08x%s\n", addr, id,
		    id == YT8531_PHY_ID ? " (Motorcomm YT8531)" : "");
		return OK;
	}

	log_warn(&dwmac_log, "no PHY answers on the MDIO bus%s\n",
	    (dwmac.info.phy_addr >= 0) ? " at the address the tree names" :
	    "");
	return ENXIO;
}

/* One of the PHY's extended registers, through the page window. */
static int
yt_read_ext(int phyaddr, uint16_t reg, uint16_t *val)
{
	int r;

	if ((r = dwmac_mdio_write(phyaddr, YT_PAGE_SELECT, reg)) != OK)
		return r;
	return dwmac_mdio_read(phyaddr, YT_PAGE_DATA, val);
}

static int
yt_modify_ext(int phyaddr, uint16_t reg, uint16_t clear, uint16_t set)
{
	uint16_t v;
	int r;

	if ((r = yt_read_ext(phyaddr, reg, &v)) != OK)
		return r;

	v = (uint16_t)((v & ~clear) | set);

	if ((r = dwmac_mdio_write(phyaddr, YT_PAGE_SELECT, reg)) != OK)
		return r;
	return dwmac_mdio_write(phyaddr, YT_PAGE_DATA, v);
}

/*
 * Turn off the PHY's own RGMII delays.
 *
 * This board does its delays in the SoC - the GRF holds the numbers and the
 * device tree says "rgmii" rather than "rgmii-id", which is exactly the
 * statement that the delays are not the PHY's job.  But the YT8531 comes
 * out of reset with its receive clock delay on, so leaving it alone means
 * two delays where the design calls for one.
 *
 * It costs a run on the board to learn this.  The symptom was as
 * misleading as it gets: receive worked perfectly, the MAC's own counter
 * said seventeen frames transmitted and seventeen of them good - and not
 * one of them ever reached the wire, because what left the MAC was sampled
 * by the PHY at the wrong moment.  Counters on this side of the delay line
 * cannot see that; only somebody else's tcpdump can.
 */
static int
yt8531_config(int phyaddr)
{
	int r;

	if ((r = yt_modify_ext(phyaddr, YT_EXT_CHIP_CONFIG,
	    YT_CHIP_CONFIG_RXC_DLY_EN, 0)) != OK)
		return r;

	return yt_modify_ext(phyaddr, YT_EXT_RGMII_CONFIG1,
	    YT_RGMII1_RX_DELAY_MASK | YT_RGMII1_FE_TX_DELAY_MASK |
	    YT_RGMII1_GE_TX_DELAY_MASK, 0);
}

/*
 * Reset the PHY over MDIO and let it negotiate.  This is the soft reset,
 * which is separate from the line the board wired to its reset pin: that
 * one has already been pulled by the time this runs.
 */
int
dwmac_phy_reset(void)
{
	uint16_t bmcr;
	unsigned waited;
	int r;

	if ((r = dwmac_mdio_write(dwmac.info.phy_addr, MII_BMCR,
	    MII_BMCR_RESET)) != OK)
		return r;

	/* The bit clears itself; the standard allows half a second. */
	for (waited = 0; waited < 500000; waited += 1000) {
		micro_delay(1000);
		if ((r = dwmac_mdio_read(dwmac.info.phy_addr, MII_BMCR,
		    &bmcr)) != OK)
			return r;
		if (!(bmcr & MII_BMCR_RESET))
			break;
	}
	if (bmcr & MII_BMCR_RESET) {
		log_warn(&dwmac_log, "the PHY did not come out of reset\n");
		return EIO;
	}

	/*
	 * After the reset, because a reset puts the extended registers back
	 * the way they were.
	 */
	if (dwmac.phy_id == YT8531_PHY_ID) {
		if ((r = yt8531_config(dwmac.info.phy_addr)) != OK) {
			log_warn(&dwmac_log, "cannot configure the YT8531: "
			    "%d\n", r);
			return r;
		}
		log_debug(&dwmac_log, "YT8531: internal rgmii delays off\n");
	}

	if (dwmac.phy_loopback) {
		log_warn(&dwmac_log, "PHY LOOPBACK: 100 Mbit/s full duplex, "
		    "nothing reaches the wire\n");
		return dwmac_mdio_write(dwmac.info.phy_addr, MII_BMCR,
		    MII_BMCR_LOOPBACK | MII_BMCR_SPEED_100 |
		    MII_BMCR_FULL_DUPLEX);
	}

	return dwmac_mdio_write(dwmac.info.phy_addr, MII_BMCR,
	    MII_BMCR_ANEG_ENABLE | MII_BMCR_ANEG_RESTART);
}

/*
 * What the PHY says about itself, for the debug dump: the two standard
 * registers, its own status word, and the two extended registers this
 * driver writes.
 */
void
dwmac_phy_dump(void)
{
	uint16_t bmcr = 0, bmsr = 0, ss = 0, cc = 0, rc = 0;
	int a = dwmac.info.phy_addr;

	if (a < 0)
		return;

	(void)dwmac_mdio_read(a, MII_BMCR, &bmcr);
	(void)dwmac_mdio_read(a, MII_BMSR, &bmsr);
	(void)dwmac_mdio_read(a, YT_SPECIFIC_STATUS, &ss);
	if (dwmac.phy_id == YT8531_PHY_ID) {
		(void)yt_read_ext(a, YT_EXT_CHIP_CONFIG, &cc);
		(void)yt_read_ext(a, YT_EXT_RGMII_CONFIG1, &rc);
	}

	log_debug(&dwmac_log, "phy: bmcr %04x bmsr %04x status %04x  "
	    "chip-config %04x rgmii-config1 %04x\n", bmcr, bmsr, ss, cc, rc);
}

/*
 * What the link is doing now.
 *
 * Speed and duplex are worked out the way clause 22 says: from what both
 * ends advertised, highest common first.  The PHY has vendor registers that
 * state the resolved mode outright, and they are not used - what is here
 * works on any PHY, and the board is not the only machine this driver will
 * ever see.
 */
int
dwmac_phy_link(unsigned *speed, int *full_duplex)
{
	uint16_t bmsr, lpa, stat1000, ctrl1000;
	int r;

	*speed = 0;
	*full_duplex = 0;

	/*
	 * Link status latches low: the first read reports whether it has
	 * been down since the last read, the second whether it is down now.
	 */
	if ((r = dwmac_mdio_read(dwmac.info.phy_addr, MII_BMSR, &bmsr)) != OK)
		return r;
	if ((r = dwmac_mdio_read(dwmac.info.phy_addr, MII_BMSR, &bmsr)) != OK)
		return r;

	if (!(bmsr & MII_BMSR_LINK))
		return OK;			/* down, and that is an answer */

	if (!(bmsr & MII_BMSR_ANEG_DONE))
		return OK;			/* still negotiating */

	if ((r = dwmac_mdio_read(dwmac.info.phy_addr, MII_LPA, &lpa)) != OK)
		return r;
	if ((r = dwmac_mdio_read(dwmac.info.phy_addr, MII_CTRL1000,
	    &ctrl1000)) != OK)
		ctrl1000 = 0;
	if ((r = dwmac_mdio_read(dwmac.info.phy_addr, MII_STAT1000,
	    &stat1000)) != OK)
		stat1000 = 0;

	if ((stat1000 & MII_STAT1000_LP_1000FD) && (ctrl1000 & (1u << 9))) {
		*speed = 1000;
		*full_duplex = 1;
	} else if ((stat1000 & MII_STAT1000_LP_1000HD) &&
	    (ctrl1000 & (1u << 8))) {
		*speed = 1000;
	} else if (lpa & MII_LPA_100FD) {
		*speed = 100;
		*full_duplex = 1;
	} else if (lpa & MII_LPA_100HD) {
		*speed = 100;
	} else if (lpa & MII_LPA_10FD) {
		*speed = 10;
		*full_duplex = 1;
	} else if (lpa & MII_LPA_10HD) {
		*speed = 10;
	}

	return OK;
}
