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

	return dwmac_mdio_write(dwmac.info.phy_addr, MII_BMCR,
	    MII_BMCR_ANEG_ENABLE | MII_BMCR_ANEG_RESTART);
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
