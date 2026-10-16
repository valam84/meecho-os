/*
 * The CDC Ethernet function (ECM) on the DWC3 core (dwc3_dev.h): what the
 * device says it is, the class request, the alternate settings of the
 * data interface, and one Ethernet frame at a time each way for lwip.
 * Descriptors and behaviour: Linux drivers/usb/gadget/function/f_ecm.c,
 * the host side drivers/net/usb/cdc_ether.c (port/clover/hw-net.md, §3).
 *
 *   interface 0   communication, Ethernet networking; its notification
 *                 endpoint EP2 IN (interrupt, 16 bytes, 32 ms) carries
 *                 NETWORK_CONNECTION and SPEED_CHANGE when the data
 *                 interface opens, as f_ecm sends them
 *   interface 1   data; alt 0 has no endpoints, alt 1 a bulk pair -
 *                 EP1 OUT and EP1 IN, 512 bytes.  The host's choosing
 *                 alt 1 is the link coming up
 *
 * A frame is one transfer: on EP1 OUT a TRB of 2048 bytes that the host's
 * short packet ends, on EP1 IN a TRB of the frame and, when its length is
 * a multiple of 512, a second TRB of none - the zero-length packet that
 * tells the host where the frame ends.  One frame waits each way; the next
 * waits in the host (NAK) or in lwip (SUSPEND).
 *
 * The station addresses are locally administered and fixed: 02:4d:45:43:
 * 48:4f for MEECHO's side, 06:… for the host's, which the host reads from
 * the iMACAddress string.
 */

#include "dwc3_dev.h"
#include "dwc3_regs.h"

#include <minix/netdriver.h>

#define USB_DT_DEVICE		1
#define USB_DT_CONFIG		2
#define USB_DT_STRING		3
#define USB_DT_QUALIFIER	6
#define USB_RECIP_MASK		0x1f
#define USB_RECIP_INTERFACE	1
#define CDC_SET_ETHERNET_PACKET_FILTER 0x43
#define CDC_NOTIFY_NETWORK_CONNECTION 0x00
#define CDC_NOTIFY_SPEED_CHANGE	0x2a
#define ECM_CTRL		0	/* interface numbers */
#define ECM_DATA		1
#define ECM_NOTIFY_LEN		16
#define ECM_NOTIFY_INTERVAL	9	/* 2^(9-1) microframes: 32 ms */
/* ecm_bitrate() at high speed: 13 packets of 512 bytes a microframe. */
#define ECM_HS_BITRATE		(13u * 512 * 8 * 1000 * 8)

static const uint8_t dev_mac[6] = { 0x02, 0x4d, 0x45, 0x43, 0x48, 0x4f };
static const uint8_t host_mac[6] = { 0x06, 0x4d, 0x45, 0x43, 0x48, 0x4f };

/*
 * The device: USB 2.00, class CDC, maxpacket 64 on ep0, 0525:a4a1 (what
 * Linux's Ethernet gadget calls itself; hosts bind CDC Ethernet by the
 * interface class, not by this), release 1.00, strings 1 and 2.
 */
static const uint8_t dev_desc[18] = {
	18, USB_DT_DEVICE, 0x00, 0x02, 0x02, 0x00, 0x00, 64,
	0x25, 0x05, 0xa1, 0xa4, 0x00, 0x01, 1, 2, 0, 1
};

static const uint8_t cfg_desc[80] = {
	/* configuration 1: two interfaces, self-powered, 500 mA */
	9, USB_DT_CONFIG, 80, 0, 2, 1, 0, 0xc0, 250,
	/* interface 0: communication, Ethernet networking, one endpoint */
	9, 4, ECM_CTRL, 0, 1, 0x02, 0x06, 0x00, 0,
	/* CDC header, 1.10 */
	5, 0x24, 0x00, 0x10, 0x01,
	/* CDC union: interface 0 controls interface 1 */
	5, 0x24, 0x06, ECM_CTRL, ECM_DATA,
	/* CDC Ethernet: MAC in string 3, no statistics, 1514, no filters */
	13, 0x24, 0x0f, 3, 0, 0, 0, 0, 0xea, 0x05, 0, 0, 0,
	/* EP2 IN: interrupt, 16 bytes, every 32 ms */
	7, 5, 0x82, 3, ECM_NOTIFY_LEN, 0, ECM_NOTIFY_INTERVAL,
	/* interface 1, alt 0: data, no endpoints */
	9, 4, ECM_DATA, 0, 0, 0x0a, 0, 0, 0,
	/* interface 1, alt 1: data, two endpoints */
	9, 4, ECM_DATA, 1, 2, 0x0a, 0, 0, 0,
	/* EP1 IN, EP1 OUT: bulk, 512 bytes */
	7, 5, 0x81, 2, 0x00, 0x02, 0,
	7, 5, 0x01, 2, 0x00, 0x02, 0
};

/* A high-speed device says what it would be at full speed. */
static const uint8_t qual_desc[10] = {
	10, USB_DT_QUALIFIER, 0x00, 0x02, 0x02, 0x00, 0x00, 64, 1, 0
};

static const char *const strings[] = {
	NULL, "MEECHO", "MEECHO USB (dwc3_ecm)"
};

static struct {
	int alt;		/* the data interface's alternate setting */
	int link;		/* alt 1: the host opened the data interface */
	int rx_ready;		/* a frame waits for lwip */
	size_t rx_len;
	int tx_busy;		/* a frame is on its way to the host */
	int notified;		/* 0 none, 1 connection, 2 speed sent */
	unsigned long rx_frames, tx_frames, rx_bad, tx_dropped;
} ecm;

#define RX	(dwc.dma + DWC3_OFF_RX)
#define TX	(dwc.dma + DWC3_OFF_TX)
#define NOTE	(dwc.dma + DWC3_OFF_NOTIFY)
#define RX_TRB	((struct dwc3_trb *)(dwc.dma + DWC3_OFF_RX_TRB))
#define TX_TRB	((struct dwc3_trb *)(dwc.dma + DWC3_OFF_TX_TRB))
#define NOTE_TRB ((struct dwc3_trb *)(dwc.dma + DWC3_OFF_NOTIFY_TRB))

/*===========================================================================*
 *    descriptors                                                             *
 *===========================================================================*/
static int
string(unsigned idx, uint8_t *buf, size_t max)
{
	static const char hex[] = "0123456789ABCDEF";
	char mac[13];
	const char *s;
	unsigned i, n;

	if (idx == 0) {			/* the languages: US English */
		buf[0] = 4;
		buf[1] = USB_DT_STRING;
		buf[2] = 0x09;
		buf[3] = 0x04;
		return 4;
	}
	if (idx == 3) {			/* the host's station address */
		for (i = 0; i < 6; i++) {
			mac[2 * i] = hex[host_mac[i] >> 4];
			mac[2 * i + 1] = hex[host_mac[i] & 0xf];
		}
		mac[12] = '\0';
		s = mac;
	} else if (idx < sizeof(strings) / sizeof(strings[0]))
		s = strings[idx];
	else
		return -1;
	n = strlen(s);
	if (2 + 2 * n > max || 2 + 2 * n > 255)
		return -1;
	buf[0] = (uint8_t)(2 + 2 * n);
	buf[1] = USB_DT_STRING;
	for (i = 0; i < n; i++) {
		buf[2 + 2 * i] = (uint8_t)s[i];
		buf[3 + 2 * i] = 0;
	}
	return (int)(2 + 2 * n);
}

static int
copy(const uint8_t *d, size_t len, uint8_t *buf, size_t max)
{
	if (len > max)
		return -1;
	memcpy(buf, d, len);
	return (int)len;
}

int
ecm_descriptor(unsigned type, unsigned idx, uint8_t *buf, size_t max)
{
	switch (type) {
	case USB_DT_DEVICE:
		return copy(dev_desc, sizeof(dev_desc), buf, max);
	case USB_DT_CONFIG:
		return idx == 0 ? copy(cfg_desc, sizeof(cfg_desc), buf, max) :
		    -1;
	case USB_DT_STRING:
		return string(idx, buf, max);
	case USB_DT_QUALIFIER:
		return copy(qual_desc, sizeof(qual_desc), buf, max);
	default:
		return -1;		/* BOS among them: no LPM offered */
	}
}

/*===========================================================================*
 *    the endpoints                                                           *
 *===========================================================================*/
static void
fill(struct dwc3_trb *t, void *buf, uint32_t len, uint32_t ctrl)
{
	phys_bytes pa = dwc3_pa(buf);

	t->bpl = (uint32_t)pa;
	t->bph = (uint32_t)((uint64_t)pa >> 32);
	t->size = len & DWC3_TRB_SIZE_MASK;
	t->ctrl = DWC3_TRBCTL_NORMAL | DWC3_TRB_CTRL_HWO | ctrl;
}

/* A receive TRB for the next frame; the host's short packet ends it. */
static void
queue_rx(void)
{
	dwc3_give(RX, DWC3_FRAME_BUF);
	fill(RX_TRB, RX, DWC3_FRAME_BUF, DWC3_TRB_CTRL_LST |
	    DWC3_TRB_CTRL_IOC | DWC3_TRB_CTRL_ISP_IMI);
	if (dwc3_ep_start(DWC3_EP1_OUT, RX_TRB) != OK)
		log_warn(&dwc3_log, "ecm: no receive transfer\n");
}

static void
notify(void)
{
	uint8_t *n = NOTE;
	uint32_t rate = ECM_HS_BITRATE;
	unsigned len;

	if (dwc.busy[DWC3_EP2_IN] || ecm.notified >= 2 || !ecm.link)
		return;
	memset(n, 0, ECM_NOTIFY_LEN);
	n[0] = 0xa1;			/* class, interface, to the host */
	n[4] = ECM_CTRL;
	if (ecm.notified == 0) {
		n[1] = CDC_NOTIFY_NETWORK_CONNECTION;
		n[2] = 1;		/* connected */
		len = 8;
	} else {
		n[1] = CDC_NOTIFY_SPEED_CHANGE;
		n[6] = 8;
		n[8] = n[12] = (uint8_t)rate;
		n[9] = n[13] = (uint8_t)(rate >> 8);
		n[10] = n[14] = (uint8_t)(rate >> 16);
		n[11] = n[15] = (uint8_t)(rate >> 24);
		len = ECM_NOTIFY_LEN;
	}
	dwc3_give(n, DWC3_LINE);
	fill(NOTE_TRB, n, len, DWC3_TRB_CTRL_LST | DWC3_TRB_CTRL_IOC);
	if (dwc3_ep_start(DWC3_EP2_IN, NOTE_TRB) == OK)
		ecm.notified++;
}

static void
set_link(int up)
{
	if (ecm.link == up)
		return;
	ecm.link = up;
	log_info(&dwc3_log, "ecm: link %s\n", up ? "up: the host opened "
	    "the data interface" : "down");
	netdriver_link();
}

static void
data_off(void)
{
	dwc3_ep_disable(DWC3_EP1_OUT);
	dwc3_ep_disable(DWC3_EP1_IN);
	ecm.rx_ready = 0;
	ecm.tx_busy = 0;
	ecm.alt = 0;
	set_link(0);
}

/* gether_connect(): the bulk pair, a receive TRB, the link, the news. */
static int
data_on(void)
{
	if (ecm.alt == 1)
		data_off();		/* again: a fresh start, as f_ecm */
	if (dwc3_ep_enable(DWC3_EP1_OUT, DWC3_EP_BULK, 512, 0) != OK ||
	    dwc3_ep_enable(DWC3_EP1_IN, DWC3_EP_BULK, 512, 0) != OK)
		return -1;
	ecm.alt = 1;
	ecm.notified = 0;
	queue_rx();
	set_link(1);
	notify();
	return 0;
}

int
ecm_set_config(unsigned config)
{
	data_off();
	dwc3_ep_disable(DWC3_EP2_IN);
	if (config == 0)
		return 0;
	/* Interface 0 at alt 0: the notification endpoint. */
	return dwc3_ep_enable(DWC3_EP2_IN, DWC3_EP_INTR, ECM_NOTIFY_LEN,
	    ECM_NOTIFY_INTERVAL) == OK ? 0 : -1;
}

int
ecm_set_interface(unsigned intf, unsigned alt)
{
	if (intf == ECM_CTRL)
		return alt == 0 ? 0 : -1;
	if (intf != ECM_DATA || alt > 1)
		return -1;
	if (alt == 0) {
		data_off();
		return 0;
	}
	return data_on();
}

int
ecm_get_interface(unsigned intf)
{
	if (intf == ECM_CTRL)
		return 0;
	if (intf == ECM_DATA)
		return ecm.alt;
	return -1;
}

/* The one class request a host sends: which frames it wants us to pass -
 * all of them, here. */
int
ecm_class_request(unsigned type, unsigned req, unsigned UNUSED(value),
	unsigned index)
{
	if (req == CDC_SET_ETHERNET_PACKET_FILTER &&
	    (type & USB_RECIP_MASK) == USB_RECIP_INTERFACE && index == ECM_CTRL)
		return 0;
	return -1;
}

void
ecm_reset(void)
{
	data_off();
	dwc3_ep_disable(DWC3_EP2_IN);
}

void
ecm_xfer_complete(unsigned ep)
{
	size_t len;

	switch (ep) {
	case DWC3_EP1_OUT:
		dwc3_take(RX_TRB, DWC3_LINE);
		len = DWC3_FRAME_BUF - (RX_TRB->size & DWC3_TRB_SIZE_MASK);
		if (len < 14 || len > NDEV_ETH_PACKET_MAX_TAGGED) {
			ecm.rx_bad++;
			netdriver_stat_ierror(1);
			queue_rx();
			return;
		}
		dwc3_take(RX, len);
		ecm.rx_len = len;
		ecm.rx_ready = 1;
		netdriver_recv();
		break;
	case DWC3_EP1_IN:
		ecm.tx_busy = 0;
		ecm.tx_frames++;
		netdriver_send();
		break;
	case DWC3_EP2_IN:
		notify();
		break;
	}
}

/*===========================================================================*
 *    libnetdriver                                                            *
 *===========================================================================*/
void
ecm_hwaddr(netdriver_addr_t *addr)
{
	memcpy(addr->na_addr, dev_mac, sizeof(dev_mac));
}

ssize_t
ecm_recv(struct netdriver_data *data, size_t max)
{
	size_t len = ecm.rx_len;

	if (!ecm.rx_ready)
		return SUSPEND;
	ecm.rx_ready = 0;
	if (len > max) {
		ecm.rx_bad++;
		netdriver_stat_ierror(1);
		queue_rx();
		return SUSPEND;
	}
	netdriver_copyout(data, 0, RX, len);
	ecm.rx_frames++;
	queue_rx();
	return (ssize_t)len;
}

int
ecm_send(struct netdriver_data *data, size_t size)
{
	struct dwc3_trb *t = TX_TRB;
	int zlp = size % 512 == 0;

	if (!ecm.link) {		/* no host to give it to */
		ecm.tx_dropped++;
		netdriver_stat_oerror(1);
		return OK;
	}
	if (ecm.tx_busy)
		return SUSPEND;
	if (size > DWC3_FRAME_BUF)
		return EINVAL;
	netdriver_copyin(data, 0, TX, size);
	dwc3_give(TX, size);
	fill(&t[0], TX, (uint32_t)size, zlp ? 0 :
	    DWC3_TRB_CTRL_LST | DWC3_TRB_CTRL_IOC);
	if (zlp)
		fill(&t[1], TX, 0, DWC3_TRB_CTRL_LST | DWC3_TRB_CTRL_IOC);
	if (dwc3_ep_start(DWC3_EP1_IN, t) != OK) {
		netdriver_stat_oerror(1);
		return OK;
	}
	ecm.tx_busy = 1;
	return OK;
}

unsigned int
ecm_get_link(uint32_t *media)
{
	if (!ecm.link)
		return NDEV_LINK_DOWN;
	*media = IFM_ETHER | IFM_FDX | IFM_1000_T;
	return NDEV_LINK_UP;
}
