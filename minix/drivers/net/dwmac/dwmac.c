/*
 * The network controller of the RK3566/RK3568: a Synopsys DWMAC 5.10 with
 * Rockchip glue around it, which on the BIGTREETECH CB2 has a Motorcomm
 * YT8531 PHY on RGMII.
 *
 * The driver was written and proved in two halves, and it is worth saying
 * which, because the halves are checked by different means.
 *
 * The first is everything up to the link: clocks, reset, pins, delay lines,
 * MDIO, the PHY.  None of it can be tested anywhere but on the board, and
 * all of it is provable in one number - the PHY identifier.  Read back what
 * the board reports and the pins are multiplexed, the pin route is right,
 * the clocks run and MDIO works.  That was one boot, and it passed.
 *
 * The second is the data path in dwmac_ring.c: descriptor rings, DMA and
 * the cache maintenance that makes memory mean the same thing to both
 * sides.  It is the first user of sys_cachectl(2), and the reason that call
 * exists.
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
	uint32_t v;

	if (dwmac_phy_link(&speed, &full_duplex) != OK)
		return;

	/* In loopback there is no partner to negotiate with; say what we set. */
	if (dwmac.phy_loopback) {
		speed = 100;
		full_duplex = 1;
	}

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

	/*
	 * And the controller itself, which needs to know two things: whether
	 * it is talking MII (10 and 100) or GMII (1000), and which of the two
	 * MII speeds.  Getting this wrong does not stop the link coming up -
	 * it makes every frame the wrong length on the wire.
	 */
	v = dwmac_rd(dwmac.mac, DWMAC_MAC_CONFIG);
	v &= ~(DWMAC_MAC_CONFIG_PS | DWMAC_MAC_CONFIG_FES |
	    DWMAC_MAC_CONFIG_DM);
	if (speed != 1000)
		v |= DWMAC_MAC_CONFIG_PS;
	if (speed == 100)
		v |= DWMAC_MAC_CONFIG_FES;
	if (full_duplex)
		v |= DWMAC_MAC_CONFIG_DM;
	dwmac_wr(dwmac.mac, DWMAC_MAC_CONFIG, v);

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

/*
 * The station address into the controller's first filter entry, so that it
 * receives what is addressed to it.  Anything else the stack wants - other
 * unicast addresses, multicast groups - would be further entries; this
 * driver does not have them yet and asks for all multicast instead.
 */
static void
dwmac_write_hwaddr(const netdriver_addr_t *addr)
{
	dwmac_wr(dwmac.mac, DWMAC_MAC_ADDR_HIGH(0), DWMAC_MAC_ADDR_HIGH_AE |
	    (uint32_t)addr->na_addr[4] | ((uint32_t)addr->na_addr[5] << 8));
	dwmac_wr(dwmac.mac, DWMAC_MAC_ADDR_LOW(0),
	    (uint32_t)addr->na_addr[0] | ((uint32_t)addr->na_addr[1] << 8) |
	    ((uint32_t)addr->na_addr[2] << 16) |
	    ((uint32_t)addr->na_addr[3] << 24));
}

/*
 * The queues between the MAC and the DMA.
 *
 * Store and forward both ways: the whole frame is in the queue before it
 * starts moving, which costs latency and buys not having to think about
 * underrun on a bus shared with everything else on the SoC.  The queue
 * sizes are read from the controller rather than assumed - the field is the
 * log2 of the FIFO in units of 128 bytes, and the size fields want it in
 * units of 256 less one.
 */
static void
dwmac_mtl_init(void)
{
	uint32_t feat, txfifo, rxfifo, v;

	feat = dwmac_rd(dwmac.mac, DWMAC_MAC_HW_FEATURE1);
	txfifo = 128u << ((feat >> DWMAC_HW_FEATURE1_TXFIFO_SHIFT) &
	    DWMAC_HW_FEATURE1_FIFO_MASK);
	rxfifo = 128u << ((feat >> DWMAC_HW_FEATURE1_RXFIFO_SHIFT) &
	    DWMAC_HW_FEATURE1_FIFO_MASK);

	log_debug(&dwmac_log, "fifo: %u bytes out, %u in\n", txfifo, rxfifo);

	v = DWMAC_MTL_OP_MODE_TSF |
	    (DWMAC_MTL_OP_MODE_TXQEN_ON << DWMAC_MTL_OP_MODE_TXQEN_SHIFT) |
	    (((txfifo / 256) - 1) << DWMAC_MTL_OP_MODE_TQS_SHIFT);
	dwmac_wr(dwmac.mac, DWMAC_MTL_TXQ_OP_MODE(0), v);
	dwmac_wr(dwmac.mac, DWMAC_MTL_TXQ_QUANTUM(0), 0x10);

	v = DWMAC_MTL_OP_MODE_RSF |
	    (((rxfifo / 256) - 1) << DWMAC_MTL_OP_MODE_RQS_SHIFT);
	dwmac_wr(dwmac.mac, DWMAC_MTL_RXQ_OP_MODE(0), v);

	/* And the one queue the MAC delivers into. */
	v = dwmac_rd(dwmac.mac, DWMAC_MAC_RXQ_CTRL0);
	v &= ~DWMAC_MAC_RXQ_CTRL0_Q0_MASK;
	v |= DWMAC_MAC_RXQ_CTRL0_Q0_DCB;
	dwmac_wr(dwmac.mac, DWMAC_MAC_RXQ_CTRL0, v);
}

/*
 * The DMA channel: where its two rings are, how long they are, and how big
 * a receive buffer it may fill.
 *
 * The ring length registers hold one less than the count, and the tail
 * pointer is an address rather than an index - the device walks physical
 * memory, so both rings are described to it by the physical addresses
 * alloc_contig() handed back.
 */
static void
dwmac_dma_init(void)
{
	uint32_t v;

	v = dwmac_rd(dwmac.mac, DWMAC_DMA_SYS_BUS_MODE);
	v |= DWMAC_DMA_SYS_BUS_MB | DWMAC_DMA_SYS_BUS_AAL |
	    DWMAC_DMA_SYS_BUS_BLEN16 | DWMAC_DMA_SYS_BUS_BLEN8 |
	    DWMAC_DMA_SYS_BUS_BLEN4;
	dwmac_wr(dwmac.mac, DWMAC_DMA_SYS_BUS_MODE, v);

	dwmac_wr(dwmac.mac, DWMAC_DMA_CH_TXDESC_HI(DWMAC_DMA_CHAN),
	    (uint32_t)(dwmac.tx_ring_phys >> 32));
	dwmac_wr(dwmac.mac, DWMAC_DMA_CH_TXDESC_LO(DWMAC_DMA_CHAN),
	    (uint32_t)dwmac.tx_ring_phys);
	dwmac_wr(dwmac.mac, DWMAC_DMA_CH_TXLEN(DWMAC_DMA_CHAN),
	    DWMAC_TX_DESCS - 1);
	dwmac_wr(dwmac.mac, DWMAC_DMA_CH_TXTAIL(DWMAC_DMA_CHAN),
	    (uint32_t)dwmac.tx_ring_phys);

	dwmac_wr(dwmac.mac, DWMAC_DMA_CH_RXDESC_HI(DWMAC_DMA_CHAN),
	    (uint32_t)(dwmac.rx_ring_phys >> 32));
	dwmac_wr(dwmac.mac, DWMAC_DMA_CH_RXDESC_LO(DWMAC_DMA_CHAN),
	    (uint32_t)dwmac.rx_ring_phys);
	dwmac_wr(dwmac.mac, DWMAC_DMA_CH_RXLEN(DWMAC_DMA_CHAN),
	    DWMAC_RX_DESCS - 1);
	dwmac_wr(dwmac.mac, DWMAC_DMA_CH_RXTAIL(DWMAC_DMA_CHAN),
	    (uint32_t)(dwmac.rx_ring_phys + DWMAC_RX_DESCS *
	    sizeof(struct dwmac_desc)));

	v = dwmac_rd(dwmac.mac, DWMAC_DMA_CH_CONTROL(DWMAC_DMA_CHAN));
	v |= DWMAC_DMA_CH_CONTROL_PBLX8;
	dwmac_wr(dwmac.mac, DWMAC_DMA_CH_CONTROL(DWMAC_DMA_CHAN), v);

	v = dwmac_rd(dwmac.mac, DWMAC_DMA_CH_RX_CONTROL(DWMAC_DMA_CHAN));
	v &= ~(DWMAC_DMA_CH_RX_CONTROL_RBSZ_MASK | DWMAC_DMA_CH_PBL_MASK);
	v |= (DWMAC_BUF_SIZE << DWMAC_DMA_CH_RX_CONTROL_RBSZ_SHIFT) &
	    DWMAC_DMA_CH_RX_CONTROL_RBSZ_MASK;
	v |= (DWMAC_DMA_PBL_DEFAULT << DWMAC_DMA_CH_PBL_SHIFT) &
	    DWMAC_DMA_CH_PBL_MASK;
	dwmac_wr(dwmac.mac, DWMAC_DMA_CH_RX_CONTROL(DWMAC_DMA_CHAN), v);

	v = dwmac_rd(dwmac.mac, DWMAC_DMA_CH_TX_CONTROL(DWMAC_DMA_CHAN));
	v &= ~DWMAC_DMA_CH_PBL_MASK;
	v |= DWMAC_DMA_CH_TX_CONTROL_OSP;
	v |= (DWMAC_DMA_PBL_DEFAULT << DWMAC_DMA_CH_PBL_SHIFT) &
	    DWMAC_DMA_CH_PBL_MASK;
	dwmac_wr(dwmac.mac, DWMAC_DMA_CH_TX_CONTROL(DWMAC_DMA_CHAN), v);
}

/*
 * What the controller thinks is going on, in one line.
 *
 * Printed on the tick at debug level, because the board is not a machine
 * one can attach a debugger to and the alternative is guessing.  The two
 * numbers that matter are the current descriptor pointers: they say whether
 * the DMA has moved at all, which separates "the driver built a bad frame"
 * from "the driver never handed one over".
 */
static void
dwmac_dump(void)
{
	log_debug(&dwmac_log, "dma st %08x  tx cur %08x head %d tail %d  "
	    "rx cur %08x next %d  mac cfg %08x  rx errors %u\n",
	    dwmac_rd(dwmac.mac, DWMAC_DMA_CH_STATUS(DWMAC_DMA_CHAN)),
	    dwmac_rd(dwmac.mac, DWMAC_DMA_CH_CUR_TXDESC(DWMAC_DMA_CHAN)),
	    dwmac.tx_head, dwmac.tx_tail,
	    dwmac_rd(dwmac.mac, DWMAC_DMA_CH_CUR_RXDESC(DWMAC_DMA_CHAN)),
	    dwmac.rx_next,
	    dwmac_rd(dwmac.mac, DWMAC_MAC_CONFIG), dwmac.rx_errors);
	log_debug(&dwmac_log, "mac counters: %u frames out (%u good), "
	    "%u frames in, %u interrupts\n",
	    dwmac_rd(dwmac.mac, DWMAC_MMC_TX_FRAMES_GB),
	    dwmac_rd(dwmac.mac, DWMAC_MMC_TX_FRAMES_G),
	    dwmac_rd(dwmac.mac, DWMAC_MMC_RX_FRAMES_GB), dwmac.irqs);
	dwmac_ring_dump();
	dwmac_phy_dump();
}

/* Let both directions run, in the controller and in the DMA. */
static void
dwmac_start(void)
{
	uint32_t v;

	dwmac_wr(dwmac.mac, DWMAC_DMA_CH_INTR_ENA(DWMAC_DMA_CHAN),
	    DWMAC_DMA_CH_INTR_NIE | DWMAC_DMA_CH_INTR_AIE |
	    DWMAC_DMA_CH_INTR_RIE | DWMAC_DMA_CH_INTR_TIE |
	    DWMAC_DMA_CH_INTR_FBE);

	v = dwmac_rd(dwmac.mac, DWMAC_DMA_CH_TX_CONTROL(DWMAC_DMA_CHAN));
	dwmac_wr(dwmac.mac, DWMAC_DMA_CH_TX_CONTROL(DWMAC_DMA_CHAN),
	    v | DWMAC_DMA_CH_TX_CONTROL_ST);

	v = dwmac_rd(dwmac.mac, DWMAC_DMA_CH_RX_CONTROL(DWMAC_DMA_CHAN));
	dwmac_wr(dwmac.mac, DWMAC_DMA_CH_RX_CONTROL(DWMAC_DMA_CHAN),
	    v | DWMAC_DMA_CH_RX_CONTROL_SR);

	v = dwmac_rd(dwmac.mac, DWMAC_MAC_CONFIG);
	dwmac_wr(dwmac.mac, DWMAC_MAC_CONFIG,
	    v | DWMAC_MAC_CONFIG_TE | DWMAC_MAC_CONFIG_RE);
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

	{
		int loop = dwmac.phy_loopback;

		memset(&dwmac, 0, sizeof(dwmac));
		dwmac.phy_loopback = loop;
	}
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
	dwmac_write_hwaddr(&dwmac.hwaddr);

	if ((r = dwmac_ring_alloc()) != OK) {
		log_warn(&dwmac_log, "no memory for the rings\n");
		return r;
	}
	dwmac_ring_init();

	dwmac_mtl_init();
	dwmac_dma_init();

	/*
	 * Receive what is addressed to this station and every multicast
	 * group.  The controller can filter multicast by hash, and this
	 * driver does not do that yet: taking them all costs the stack a few
	 * frames to discard and is honest about what the hardware is doing.
	 */
	dwmac_wr(dwmac.mac, DWMAC_MAC_PACKET_FILTER,
	    DWMAC_MAC_PACKET_FILTER_PM);

	if ((r = sys_irqsetpolicy(dwmac.info.irq, 0, &dwmac.irq_hook)) != OK) {
		log_warn(&dwmac_log, "cannot take interrupt %d: %d\n",
		    dwmac.info.irq, r);
		return r;
	}
	if ((r = sys_irqenable(&dwmac.irq_hook)) != OK) {
		log_warn(&dwmac_log, "cannot enable interrupt %d: %d\n",
		    dwmac.info.irq, r);
		return r;
	}
	dwmac.irq_enabled = 1;

	dwmac_start();

	*caps = NDEV_CAP_BCAST | NDEV_CAP_MCAST;
	*ticks = sys_hz() / DWMAC_TICK_HZ;

	dwmac_link_check();

	return OK;
}

static void
dwmac_stop(void)
{
	dwmac_wr(dwmac.mac, DWMAC_MAC_CONFIG,
	    dwmac_rd(dwmac.mac, DWMAC_MAC_CONFIG) &
	    ~(DWMAC_MAC_CONFIG_RE | DWMAC_MAC_CONFIG_TE));

	dwmac_wr(dwmac.mac, DWMAC_DMA_CH_TX_CONTROL(DWMAC_DMA_CHAN),
	    dwmac_rd(dwmac.mac, DWMAC_DMA_CH_TX_CONTROL(DWMAC_DMA_CHAN)) &
	    ~DWMAC_DMA_CH_TX_CONTROL_ST);
	dwmac_wr(dwmac.mac, DWMAC_DMA_CH_RX_CONTROL(DWMAC_DMA_CHAN),
	    dwmac_rd(dwmac.mac, DWMAC_DMA_CH_RX_CONTROL(DWMAC_DMA_CHAN)) &
	    ~DWMAC_DMA_CH_RX_CONTROL_SR);

	dwmac_wr(dwmac.mac, DWMAC_DMA_CH_INTR_ENA(DWMAC_DMA_CHAN), 0);

	if (dwmac.irq_enabled) {
		(void)sys_irqdisable(&dwmac.irq_hook);
		(void)sys_irqrmpolicy(&dwmac.irq_hook);
		dwmac.irq_enabled = 0;
	}

	dwmac_ring_free();
}

static void
dwmac_tick(void)
{
	dwmac_link_check();
	dwmac_dump();
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

static ssize_t
dwmac_recv(struct netdriver_data *data, size_t max)
{
	return dwmac_ring_recv(data, max);
}

static int
dwmac_send(struct netdriver_data *data, size_t size)
{
	return dwmac_ring_send(data, size);
}

/*
 * The interrupt.
 *
 * One line for the whole controller, and the channel's status register says
 * which of the two directions wants attention.  The status bits are cleared
 * by writing them back - a read does not clear them, and a driver that
 * forgets that takes the same interrupt for ever.
 *
 * Both directions are then simply announced to libnetdriver, which comes
 * back through recv and send; there is nothing to do here that those two do
 * not do better, and doing the work here would mean doing it with the
 * interrupt still masked.
 */
static void
dwmac_intr(unsigned int __unused mask)
{
	uint32_t status;
	int r;

	status = dwmac_rd(dwmac.mac, DWMAC_DMA_CH_STATUS(DWMAC_DMA_CHAN));
	dwmac_wr(dwmac.mac, DWMAC_DMA_CH_STATUS(DWMAC_DMA_CHAN), status);
	dwmac.irqs++;

	if (status & (DWMAC_DMA_CH_STATUS_RI | DWMAC_DMA_CH_STATUS_RBU))
		netdriver_recv();

	if (status & (DWMAC_DMA_CH_STATUS_TI | DWMAC_DMA_CH_STATUS_TBU))
		netdriver_send();

	/*
	 * A bus error is the one thing worth saying out loud: it means the
	 * controller could not reach memory, which is a driver mistake about
	 * an address rather than anything the network did.
	 */
	if (status & DWMAC_DMA_CH_STATUS_FBE)
		log_warn(&dwmac_log, "bus error, dma status 0x%08x\n", status);

	if ((r = sys_irqenable(&dwmac.irq_hook)) != OK)
		log_warn(&dwmac_log, "cannot re-enable the interrupt: %d\n",
		    r);
}

static const struct netdriver dwmac_table = {
	.ndr_name	= "dwm",
	.ndr_init	= dwmac_init,
	.ndr_stop	= dwmac_stop,
	.ndr_recv	= dwmac_recv,
	.ndr_send	= dwmac_send,
	.ndr_get_link	= dwmac_get_link,
	.ndr_intr	= dwmac_intr,
	.ndr_tick	= dwmac_tick,
};

int
main(int argc, char *argv[])
{
	long v;

	env_setargs(argc, argv);

	/* How much this driver says, the way sdmmc takes it. */
	if (env_parse("log", "d", 0, &v, LEVEL_NONE, LEVEL_TRACE) == EP_SET)
		dwmac_log.log_level = (int)v;

	/*
	 * Diagnostic: fold the PHY back on itself.  A frame the MAC sends
	 * then comes back to the MAC without touching the wire, which
	 * splits "the controller cannot talk to the PHY" from "the PHY
	 * cannot talk to the network" - two problems that look identical
	 * from the console and have nothing in common.
	 */
	if (env_parse("phyloop", "d", 0, &v, 0, 1) == EP_SET)
		dwmac.phy_loopback = (int)v;

	netdriver_task(&dwmac_table);

	return 0;
}
