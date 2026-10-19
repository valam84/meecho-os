/*
 * The modem's data path as a network interface for lwip ("ipa0"): lwip
 * knows Ethernet interfaces only, the IPA carries raw IP packets behind a
 * 4-byte QMAP header (byte 0: command bit and padding, 1: the mux id,
 * 2-3: the length, big-endian) - rmnet's MAP, as ipa2-lite's rmnet_ipa0
 * carries it (port/clover/hw-modem.md, section 9).  So this side of the
 * interface is a pretend Ethernet with one neighbour: every ARP request
 * is answered here with the neighbour's address, every frame to it loses
 * its Ethernet header and gains a QMAP one on the TX pipe (4), and every
 * packet from the RX pipe (5) comes up with an Ethernet header from the
 * neighbour to us.  The link is up once the modem's IPA driver has
 * finished the handshake (ipaqmid, IPAIOC_MODEM).
 */

#include "ipa.h"

#include <minix/netdriver.h>

#include <string.h>

#define NRX		32
#define NTX		32
#define BUF		2048
#define ETH_HDR		14
#define QMAP_HDR	4

#define DESC_EOT	(1u << 14)
#define DESC_INT	(1u << 15)

#define BAM_P_RD_OFF(n)	(IPA_BAM + 0x13800 + 0x1000 * (n))
#define IPA_EP_CTRL(n)	(IPA_REGS + 0x0070 + 4 * (n))
#define IPA_EP_HOL(n)	(IPA_REGS + 0x03c0 + 4 * (n))

static const uint8_t us[6] = { 0x02, 0x4d, 0x45, 0x49, 0x50, 0x41 };
static const uint8_t peer[6] = { 0x02, 0x4d, 0x45, 0x49, 0x50, 0x4d };

static struct dbuf rx[NRX], tx[NTX];
static unsigned rx_slot_buf[FIFO_NDESC];	/* rx[] behind each slot */
static unsigned tx_head, tx_tail;		/* in tx[] */
static int up, started, tx_waiting;
static uint8_t mux_id = 1;

static uint8_t arp_reply[42];
static int arp_pending;

static unsigned
rd_index(unsigned ep)
{
	return (rd32(ipa.base, BAM_P_RD_OFF(ep)) & 0xffff) / DESC_BYTES &
	    (FIFO_NDESC - 1);
}

static int
post_rx(unsigned i)
{
	struct bam_desc d;
	unsigned slot = hw.pipe[EP_RX].tail;

	dbuf_inval(&rx[i], 0, BUF);
	d.addr = rx[i].iova;
	d.size = BUF;
	d.flags = DESC_INT;
	rx_slot_buf[slot] = i;
	return pipe_put(EP_RX, &d, 1);
}

int
ipa_net_init(unsigned mux)
{
	unsigned i;
	int r;

	mux_id = (uint8_t)mux;
	for (i = 0; i < NRX; i++)
		if ((r = dbuf_alloc(&rx[i], BUF, 1)) != OK)
			return r;
	for (i = 0; i < NTX; i++)
		if ((r = dbuf_alloc(&tx[i], BUF, 0)) != OK)
			return r;
	return OK;
}

/* The handshake ended (1) or the modem went away (0). */
void
ipa_net_modem(int ready)
{
	unsigned i;

	if (ready && !started) {
		for (i = 0; i < NRX; i++)
			if (post_rx(i) != OK)
				break;
		wr32(ipa.base, IPA_EP_HOL(EP_RX), 0);
		wr32(ipa.base, IPA_EP_CTRL(EP_RX), 0);
		wr32(ipa.base, IPA_EP_CTRL(EP_TX), 0);
		started = 1;
		log_info(&ilog, "ipa0: %u receive buffers posted, mux id %u\n",
		    i, mux_id);
	}
	if (ready != up) {
		up = ready;
		netdriver_link();
	}
}

unsigned int
ipa_net_link(uint32_t *media)
{
	*media = IFM_ETHER | IFM_AUTO;
	return up ? NDEV_LINK_UP : NDEV_LINK_DOWN;
}

void
ipa_net_hwaddr(netdriver_addr_t *a)
{
	memcpy(a->na_addr, us, sizeof(us));
}

/* An ARP request for anyone: the neighbour answers it. */
static void
arp(const uint8_t *f, size_t n)
{
	uint8_t *r = arp_reply;

	if (n < 42 || f[20] != 0 || f[21] != 1)	/* not a request */
		return;
	memcpy(r, f + 6, 6);			/* to the asker */
	memcpy(r + 6, peer, 6);
	r[12] = 0x08; r[13] = 0x06;
	memcpy(r + 14, f + 14, 6);		/* htype, ptype, hlen, plen */
	r[20] = 0; r[21] = 2;			/* reply */
	memcpy(r + 22, peer, 6);		/* sender: the neighbour, */
	memcpy(r + 28, f + 38, 4);		/* with the address asked */
	memcpy(r + 32, f + 22, 6);		/* target: the asker */
	memcpy(r + 38, f + 28, 4);
	arp_pending = 1;
	netdriver_recv();
}

int
ipa_net_send(struct netdriver_data *data, size_t size)
{
	uint8_t f[42], *b;
	struct bam_desc d;
	unsigned type, len;
	int r;

	if (size < ETH_HDR)
		return EINVAL;
	netdriver_copyin(data, 0, f, size < sizeof(f) ? size : sizeof(f));
	type = f[12] << 8 | f[13];
	if (type == 0x0806) {
		arp(f, size);
		return OK;
	}
	if ((type != 0x0800 && type != 0x86dd) || !up)
		return OK;		/* dropped: nothing to carry it */
	len = (unsigned)size - ETH_HDR;
	if (len + QMAP_HDR > BUF)
		return EINVAL;
	if (((tx_tail + 1) % NTX) == tx_head) {
		tx_waiting = 1;
		return SUSPEND;
	}
	b = tx[tx_tail].va;
	b[0] = 0;
	b[1] = mux_id;
	b[2] = len >> 8;
	b[3] = len & 0xff;
	netdriver_copyin(data, ETH_HDR, b + QMAP_HDR, len);
	dbuf_clean(&tx[tx_tail], 0, len + QMAP_HDR);
	d.addr = tx[tx_tail].iova;
	d.size = (uint16_t)(len + QMAP_HDR);
	d.flags = DESC_EOT;
	if ((r = pipe_put(EP_TX, &d, 1)) != OK) {
		tx_waiting = 1;
		return SUSPEND;
	}
	tx_tail = (tx_tail + 1) % NTX;
	return OK;
}

ssize_t
ipa_net_recv(struct netdriver_data *data, size_t max)
{
	struct pipe *p = &hw.pipe[EP_RX];
	uint8_t eth[ETH_HDR], *b;
	unsigned slot, i, len, plen;

	if (arp_pending) {
		arp_pending = 0;
		if (max < sizeof(arp_reply))
			return SUSPEND;
		netdriver_copyout(data, 0, arp_reply, sizeof(arp_reply));
		return sizeof(arp_reply);
	}
	while (started && p->head != rd_index(EP_RX)) {
		slot = p->head;
		p->head = (p->head + 1) & (FIFO_NDESC - 1);
		dbuf_inval(&p->fifo, slot * DESC_BYTES, DESC_BYTES);
		len = p->desc[slot].size;
		i = rx_slot_buf[slot];
		dbuf_inval(&rx[i], 0, BUF);
		b = rx[i].va;
		plen = b[2] << 8 | b[3];
		if (len < QMAP_HDR + 20 || (b[0] & 0x80) ||
		    plen > len - QMAP_HDR || ETH_HDR + plen > max) {
			netdriver_stat_ierror(1);
			(void)post_rx(i);
			continue;
		}
		plen -= b[0] & 0x3f;			/* padding */
		memcpy(eth, us, 6);
		memcpy(eth + 6, peer, 6);
		eth[12] = (b[QMAP_HDR] >> 4) == 6 ? 0x86 : 0x08;
		eth[13] = (b[QMAP_HDR] >> 4) == 6 ? 0xdd : 0x00;
		netdriver_copyout(data, 0, eth, ETH_HDR);
		netdriver_copyout(data, ETH_HDR, b + QMAP_HDR, plen);
		(void)post_rx(i);
		return ETH_HDR + plen;
	}
	return SUSPEND;
}

/* Every tick: what the TX pipe took, what the RX pipe brought. */
void
ipa_net_tick(void)
{
	struct pipe *p = &hw.pipe[EP_TX];
	unsigned rd;

	if (!started)
		return;
	rd = rd_index(EP_TX);
	while (p->head != rd) {
		p->head = (p->head + 1) & (FIFO_NDESC - 1);
		tx_head = (tx_head + 1) % NTX;
	}
	if (tx_waiting && ((tx_tail + 1) % NTX) != tx_head) {
		tx_waiting = 0;
		netdriver_send();
	}
	if (hw.pipe[EP_RX].head != rd_index(EP_RX))
		netdriver_recv();
}
