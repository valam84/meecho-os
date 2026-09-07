/*
 * The network controller of the RK3566/RK3568: a Synopsys DWMAC 5.10 with
 * Rockchip glue around it, which on the BIGTREETECH CB2 has a Motorcomm
 * YT8531 PHY on RGMII.
 *
 * This is the first half of the driver.  It brings the controller up -
 * clocks, reset, pins, delay lines, MDIO, the PHY and the link - and stops
 * there: the descriptor rings and the data path are not here yet, so the
 * interface exists, reports its link and carries no packets.
 *
 * The split is deliberate rather than unfinished.  Everything up to and
 * including the link is what cannot be tested anywhere but on the board,
 * and it is provable in one number: the PHY identifier.  If that reads back
 * as what the board reports, then the pins are multiplexed, the pin route
 * is right, the clocks run and MDIO works - and if it does not, none of the
 * harder work that follows would have had a chance.  Getting that far and
 * saying so is worth a boot of its own.
 */

#include "dwmac.h"
#include "dwmacreg.h"

struct dwmac dwmac;

struct log dwmac_log = {
	.name = "dwmac",
	.log_level = LEVEL_INFO,
	.log_func = default_log
};

/*
 * How often the link is looked at.  libnetdriver calls the tick at whatever
 * period the driver asked for at init; a second is what a link that changes
 * by somebody moving a cable deserves, and it costs six MDIO reads.
 */
#define DWMAC_TICK_HZ		1

static void
dwmac_link_check(void)
{
	unsigned speed;
	int full_duplex;

	if (dwmac_phy_link(&speed, &full_duplex) != OK)
		return;

	if (speed == dwmac.speed && full_duplex == dwmac.full_duplex)
		return;

	dwmac.speed = speed;
	dwmac.full_duplex = full_duplex;

	/*
	 * Tell libnetdriver, which tells the stack.  Without this the driver
	 * knows the link is up and nobody else does: through the first run on
	 * the board ifconfig went on saying "no carrier" while the console
	 * said "link up: 100 Mbit/s" two lines above it.
	 */
	netdriver_link();

	if (speed == 0) {
		log_info(&dwmac_log, "link down\n");
		return;
	}

	/*
	 * The speed the link settled on decides the rate of the transmit
	 * clock, and that is a CRU register on this SoC rather than anything
	 * in the controller - see dwmac_rk_set_speed().
	 */
	dwmac_rk_set_speed(speed);

	log_info(&dwmac_log, "link up: %u Mbit/s, %s duplex\n", speed,
	    full_duplex ? "full" : "half");
}

/*
 * The station address.
 *
 * The controller keeps one across a soft reset, and on this board the
 * bootloader does not put one there - the vendor system derives it from a
 * fuse in the SoC, which is a thing this driver has no reader for. So what
 * is here is whatever the hardware has, and if that is nothing, a locally
 * administered address made from the controller's own address. Ugly, and
 * honest: it is stable across boots of this machine and says of itself that
 * it was not assigned by anybody.
 */
static void
dwmac_read_hwaddr(netdriver_addr_t *addr)
{
	uint32_t hi, lo;

	hi = dwmac_rd(dwmac.mac, DWMAC_MAC_ADDR_HIGH(0));
	lo = dwmac_rd(dwmac.mac, DWMAC_MAC_ADDR_LOW(0));

	addr->na_addr[0] = (uint8_t)(lo & 0xff);
	addr->na_addr[1] = (uint8_t)((lo >> 8) & 0xff);
	addr->na_addr[2] = (uint8_t)((lo >> 16) & 0xff);
	addr->na_addr[3] = (uint8_t)((lo >> 24) & 0xff);
	addr->na_addr[4] = (uint8_t)(hi & 0xff);
	addr->na_addr[5] = (uint8_t)((hi >> 8) & 0xff);

	if ((addr->na_addr[0] | addr->na_addr[1] | addr->na_addr[2] |
	    addr->na_addr[3] | addr->na_addr[4] | addr->na_addr[5]) == 0 ||
	    (addr->na_addr[0] & 1)) {
		addr->na_addr[0] = 0x02;	/* locally administered */
		addr->na_addr[1] = 0x00;
		addr->na_addr[2] = (uint8_t)(dwmac.info.base >> 24);
		addr->na_addr[3] = (uint8_t)(dwmac.info.base >> 16);
		addr->na_addr[4] = (uint8_t)(dwmac.info.base >> 8);
		addr->na_addr[5] = (uint8_t)(dwmac.info.base);

		log_warn(&dwmac_log, "no station address in the controller; "
		    "using a locally administered one\n");
	}
}

static int
dwmac_soft_reset(void)
{
	unsigned waited;

	dwmac_wr(dwmac.mac, DWMAC_DMA_BUS_MODE,
	    dwmac_rd(dwmac.mac, DWMAC_DMA_BUS_MODE) |
	    DWMAC_DMA_BUS_MODE_SWR);

	/* The bit clears itself when the block is back. */
	for (waited = 0; waited < 100000; waited += 100) {
		if (!(dwmac_rd(dwmac.mac, DWMAC_DMA_BUS_MODE) &
		    DWMAC_DMA_BUS_MODE_SWR))
			return OK;
		micro_delay(100);
	}

	log_warn(&dwmac_log, "the controller did not finish its reset\n");
	return EIO;
}

static int
dwmac_init(unsigned int instance, netdriver_addr_t *addr, uint32_t *caps,
	unsigned int *ticks)
{
	uint32_t version;
	int r;

	memset(&dwmac, 0, sizeof(dwmac));
	dwmac.info.phy_addr = -1;
	dwmac.info.reset_pin = -1;

	if ((r = dwmac_find(&dwmac.info, (int)instance)) != OK)
		return r;

	if ((r = dwmac_rk_map()) != OK)
		return r;

	/*
	 * The glue, in the order the vendor driver does it: clocks first
	 * because nothing answers without them, then the block's own reset,
	 * then the pins, then the interface mode and its delay lines.
	 */
	dwmac_rk_clocks_on();
	dwmac_rk_reset_controller();
	dwmac_rk_pins();
	dwmac_rk_rgmii();

	/*
	 * And now the first thing that can be checked. The low byte is the
	 * Synopsys identifier and the byte above it the vendor's; the board
	 * reports 0x51 and 0x30. Anything else here means the registers are
	 * not really there, and everything after this point would be
	 * guesswork on top of a bad foundation.
	 */
	version = dwmac_rd(dwmac.mac, DWMAC_VERSION);
	log_info(&dwmac_log, "synopsys id 0x%02x, user id 0x%02x\n",
	    DWMAC_VERSION_SNPS(version), DWMAC_VERSION_USER(version));

	if (DWMAC_VERSION_SNPS(version) == 0x00 ||
	    DWMAC_VERSION_SNPS(version) == 0xff) {
		log_warn(&dwmac_log, "the controller does not answer; its "
		    "clocks or its reset are wrong\n");
		return ENXIO;
	}
	if (DWMAC_VERSION_SNPS(version) < DWMAC_SNPS_ID_5_10)
		log_warn(&dwmac_log, "this is an older core than the one "
		    "this driver was written against (0x%02x)\n",
		    DWMAC_SNPS_ID_5_10);

	if ((r = dwmac_soft_reset()) != OK)
		return r;

	/* The PHY: the board's line first, then the one over MDIO. */
	dwmac_rk_reset_phy();

	if ((r = dwmac_phy_find()) != OK)
		return r;
	if ((r = dwmac_phy_reset()) != OK)
		return r;

	dwmac_read_hwaddr(&dwmac.hwaddr);
	*addr = dwmac.hwaddr;

	/*
	 * No capabilities are claimed and no interrupt is taken yet: with no
	 * descriptor rings there is nothing for either to do. The link is
	 * looked at on the tick.
	 */
	*caps = 0;
	*ticks = sys_hz() / DWMAC_TICK_HZ;

	dwmac_link_check();

	log_info(&dwmac_log, "up, but carrying no traffic yet: the "
	    "descriptor rings are the next half of this driver\n");

	return OK;
}

static void
dwmac_stop(void)
{
	dwmac_wr(dwmac.mac, DWMAC_MAC_CONFIG,
	    dwmac_rd(dwmac.mac, DWMAC_MAC_CONFIG) &
	    ~(DWMAC_MAC_CONFIG_RE | DWMAC_MAC_CONFIG_TE));
}

static void
dwmac_tick(void)
{
	dwmac_link_check();
}

static unsigned int
dwmac_get_link(uint32_t *media)
{
	if (dwmac.speed == 0)
		return NDEV_LINK_DOWN;

	*media = IFM_ETHER | (dwmac.full_duplex ? IFM_FDX : IFM_HDX);
	switch (dwmac.speed) {
	case 10:	*media |= IFM_10_T;	break;
	case 100:	*media |= IFM_100_TX;	break;
	case 1000:	*media |= IFM_1000_T;	break;
	}

	return NDEV_LINK_UP;
}

/*
 * Receive and transmit: not yet.
 *
 * SUSPEND is "nothing has arrived", which is true and will go on being true
 * until the rings exist. A send is accepted and dropped, which is what an
 * ethernet driver is allowed to do with a frame anyway - and saying so out
 * loud once is better than a stack that waits forever for a reply.
 */
static ssize_t
dwmac_recv(struct netdriver_data *data __unused, size_t max __unused)
{
	return SUSPEND;
}

static int
dwmac_send(struct netdriver_data *data __unused, size_t size __unused)
{
	static int said;

	if (!said) {
		log_warn(&dwmac_log, "dropping frames: this driver has no "
		    "data path yet\n");
		said = 1;
	}

	return OK;
}

static const struct netdriver dwmac_table = {
	.ndr_name	= "dwm",
	.ndr_init	= dwmac_init,
	.ndr_stop	= dwmac_stop,
	.ndr_recv	= dwmac_recv,
	.ndr_send	= dwmac_send,
	.ndr_get_link	= dwmac_get_link,
	.ndr_tick	= dwmac_tick,
};

int
main(int argc, char *argv[])
{
	env_setargs(argc, argv);

	netdriver_task(&dwmac_table);

	return 0;
}
