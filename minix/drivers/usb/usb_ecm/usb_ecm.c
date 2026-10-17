/*
 * CDC Ethernet on the host side: a USB device that is a network interface -
 * an Ethernet adapter, or the Mi Pad 4's own function (minix/drivers/net/
 * dwc3_ecm) - as an interface lwip can use.
 *
 * Where it sits.  Below it is the USB host controller service (label
 * "usbd" - xhci on this port), spoken to through libusb's URB protocol;
 * xhci announces a device here when one of its interfaces is 2/6/0.  Above
 * it is lwip, spoken to through libnetdriver as an Ethernet driver named
 * cdce, which is what NetBSD calls this driver; the interface is cdce0.
 *
 * What the device is asked, one control request at a time, as Linux's
 * cdc_ether and usbnet ask it (drivers/net/usb/cdc_ether.c, usbnet.c):
 *
 *   GET_CONFIGURATION, and the configuration descriptor with that value -
 *       the interfaces, the endpoints, the Ethernet descriptor
 *       (ecm_desc.c)
 *   string 0 for a language, then string iMACAddress: the interface's
 *       station address, twelve hex digits, without which Linux refuses
 *       the device and so does this
 *   SET_INTERFACE(data, alternate with the bulk pair): the link opens.
 *       xhci tells its controller about the bulk pair as the request goes
 *   SET_ETHERNET_PACKET_FILTER: directed and broadcast frames, multicast
 *       when lwip asks for any (ndr_set_mode); stalled, it is forgiven
 *
 * and then two transfers wait with no deadline: one for a notification
 * (NETWORK_CONNECTION moves the link, SPEED_CHANGE the media word) and one
 * for a frame, a bulk IN with USB_URB_WAITS.  A frame out is one bulk OUT
 * transfer, one at a time.  A frame in that is shorter than Ethernet's
 * minimum is padded with zeros - USB carries frames as long as they are,
 * and libnetdriver drops anything under NDEV_ETH_PACKET_MIN; the tablet's
 * side learned that from an ARP request that was never answered
 * (port/PORTING-LOG.md, the Mi Pad 4's CDC Ethernet, 2026-10-16).
 *
 * The interface appears in lwip only once a device has said its station
 * address: libnetdriver announces a driver with the address its init
 * returned, and that address is the device's, not this driver's to make
 * up.  So netdriver_init() runs from the main loop below when the first
 * device is open, and not from SEF's start.  A device that goes away and
 * comes back is driven again under the same interface; one with another
 * address is not - the interface keeps the first one - and the log says
 * so.
 *
 * The rule of usb_kbd, for its reason: xhci SENDS the completion of a
 * control or bulk OUT transfer, and a driver blocked in sendrec at that
 * moment takes it for the answer to its request, upon which libusb panics.
 * So nothing goes to xhci while such a transfer is out; what wants to go
 * meanwhile - a receive, a notification, a frame, a control request -
 * waits for it and goes from kick().
 *
 * Arguments (minix-service up ... -args): log=0..4.
 */

#include <minix/drivers.h>
#include <minix/ds.h>
#include <minix/log.h>
#include <minix/netdriver.h>
#include <minix/usb.h>

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "ecm_desc.h"

static struct log ecm_log = {
	.name = "usb_ecm",
	.log_level = LEVEL_INFO,
	.log_func = default_log
};

/*
 * The request blocks, one per kind of transfer, each a struct usb_urb
 * followed by its data.  2048 bytes is a frame (1514, 1518 tagged, one
 * more for the padding byte) rounded up to high-speed packets, and what
 * xhci holds for a waiting transfer.
 */
#define CTL_DATA	1024		/* a configuration descriptor */
#define FRAME_DATA	2048
#define NOTE_DATA	64		/* one packet of a notification */

static char ctl_mem[USB_URBSIZE(CTL_DATA, 0)] __attribute__((aligned(8)));
static char rx_mem[USB_URBSIZE(FRAME_DATA, 0)] __attribute__((aligned(8)));
static char tx_mem[USB_URBSIZE(FRAME_DATA, 0)] __attribute__((aligned(8)));
static char note_mem[USB_URBSIZE(NOTE_DATA, 0)] __attribute__((aligned(8)));
#define CTL	((struct usb_urb *)ctl_mem)
#define RX	((struct usb_urb *)rx_mem)
#define TX	((struct usb_urb *)tx_mem)
#define NOTE	((struct usb_urb *)note_mem)
#define URB_HEADER	offsetof(struct usb_urb, buffer)

/* Which way a transfer goes, in the value that travels (see usb_kbd.c). */
#define DIR_IN		1
#define DIR_OUT		0

/* Requests: USB 2.0, 9.4; CDC ECM 1.2, 6.2. */
#define REQ_GET_DESCRIPTOR	6
#define REQ_GET_CONFIGURATION	8
#define REQ_SET_INTERFACE	11
#define CDC_SET_ETHERNET_PACKET_FILTER	0x43
#define DT_CONFIG		2
#define DT_STRING		3

#define MAX_ERRORS	50		/* in a row; then the device is let go */

/* Where a device is in being set up; one control request per step. */
enum stage {
	ST_NONE,		/* no device */
	ST_GETCONF,		/* GET_CONFIGURATION */
	ST_CONFIG,		/* the configuration descriptor, by index */
	ST_LANG,		/* string 0 */
	ST_MAC,			/* string iMACAddress */
	ST_ALT,			/* SET_INTERFACE: the data interface opens */
	ST_RUNNING		/* frames, and the filter as asked */
};

static struct {
	enum stage stage;
	unsigned dev_id;
	struct ecm_info info;
	unsigned config;	/* the value GET_CONFIGURATION gave; 0 none */
	unsigned cfg_index;	/* which descriptor is being read */
	unsigned langid;
	uint8_t mac[6];
	int ctl_wanted;		/* the stage's request is owed */

	int link;		/* the device says it is connected */
	uint32_t speed;		/* bits per second; 0 not said */
	unsigned filter_set;	/* what the device was told; ~0 nothing */
	struct ecm_notes notes;

	int rx_ready;		/* a frame in RX for lwip */
	size_t rx_len;
	int rx_wanted, note_wanted;	/* to be asked for when it can */
	int rx_retry, note_retry;	/* ...at the next tick, after a failure */
	unsigned rx_errors, note_errors;

	unsigned long rx_frames, tx_frames, rx_bad, tx_dropped;
} ecm;

/*
 * Which blocks are with xhci.  Kept apart from the device's state, which
 * forget() clears: a completion still owed for a device that has gone is
 * still owed, and while one of the first two is out nothing may go to
 * xhci at all.
 */
static int ctl_out, tx_out;	/* completion sent: nothing to xhci meanwhile */
static int rx_out, note_out;	/* completion posted: waiting on the ring */

static endpoint_t usbd_ep = NONE;
static int nd_up;		/* netdriver_init() has run: lwip knows us */
static uint8_t nd_mac[6];	/* with this address */
static unsigned nd_mode = NDEV_MODE_DOWN;
static int running = 1;

static void ecm_urb_completion(struct usb_urb *urb);
static void ecm_connect(unsigned dev_id, unsigned interfaces);
static void ecm_disconnect(unsigned dev_id);
static void kick(void);
static const struct netdriver ecm_table;

static struct usb_driver ecm_usb = {
	.urb_completion		= ecm_urb_completion,
	.connect_device		= ecm_connect,
	.disconnect_device	= ecm_disconnect
};

/* A station address for the log; two may be wanted in one line. */
static const char *
mac_str(const uint8_t *m)
{
	static char s[2][18];
	static unsigned which;
	char *p = s[which++ & 1];

	snprintf(p, sizeof(s[0]), "%02x:%02x:%02x:%02x:%02x:%02x", m[0], m[1],
	    m[2], m[3], m[4], m[5]);
	return p;
}

/*===========================================================================*
 *    requests to xhci                                                       *
 *===========================================================================*/
static void
send_ctl(uint8_t type, uint8_t request, uint16_t value, uint16_t index,
	uint16_t length)
{
	struct usb_ctrlrequest *req;

	memset(ctl_mem, 0, URB_HEADER);
	CTL->urb_size = sizeof(ctl_mem);
	CTL->dev_id = (int)ecm.dev_id;
	CTL->type = USB_TRANSFER_CTL;
	CTL->endpoint = 0;
	CTL->direction = (type & 0x80) ? DIR_IN : DIR_OUT;
	CTL->size = length;

	req = (struct usb_ctrlrequest *)CTL->setup_packet;
	req->bRequestType = type;
	req->bRequest = request;
	req->wValue = value;
	req->wIndex = index;
	req->wLength = length;

	log_debug(&ecm_log, "request 0x%02x 0x%02x value 0x%04x index %u, %u "
	    "byte(s)\n", type, request, value, index, length);

	ctl_out = 1;
	if (usb_send_urb(CTL) != OK) {
		ctl_out = 0;
		log_warn(&ecm_log, "control request 0x%02x could not be sent\n",
		    request);
	}
}

/* The stage's own request: the next question to the device. */
static void
stage_request(void)
{
	switch (ecm.stage) {
	case ST_GETCONF:
		send_ctl(0x80, REQ_GET_CONFIGURATION, 0, 0, 1);
		break;
	case ST_CONFIG:
		send_ctl(0x80, REQ_GET_DESCRIPTOR,
		    (DT_CONFIG << 8) | ecm.cfg_index, 0, CTL_DATA);
		break;
	case ST_LANG:
		send_ctl(0x80, REQ_GET_DESCRIPTOR, DT_STRING << 8, 0, 255);
		break;
	case ST_MAC:
		send_ctl(0x80, REQ_GET_DESCRIPTOR,
		    (DT_STRING << 8) | ecm.info.mac_string, ecm.langid, 255);
		break;
	case ST_ALT:
		send_ctl(0x01, REQ_SET_INTERFACE, ecm.info.data_alt,
		    ecm.info.data_iface, 0);
		break;
	default:
		break;
	}
}

/* What lwip asked to receive, as the device's filter (Linux's choice). */
static unsigned
mode_filter(unsigned mode)
{
	unsigned f = ECM_FILTER_DIRECTED | ECM_FILTER_BROADCAST;

	if (mode & (NDEV_MODE_MCAST_LIST | NDEV_MODE_MCAST_ALL))
		f |= ECM_FILTER_ALL_MULTICAST;
	if (mode & NDEV_MODE_PROMISC)
		f |= ECM_FILTER_PROMISCUOUS;
	return f;
}

static void
send_filter(unsigned filter)
{
	ecm.filter_set = filter;
	send_ctl(0x21, CDC_SET_ETHERNET_PACKET_FILTER, filter,
	    ecm.info.ctrl_iface, 0);
}

/* A frame may come: the bulk IN waits on its endpoint for one. */
static void
submit_rx(void)
{
	memset(rx_mem, 0, URB_HEADER);
	RX->urb_size = sizeof(rx_mem);
	RX->dev_id = (int)ecm.dev_id;
	RX->type = USB_TRANSFER_BLK;
	RX->endpoint = (int)ecm.info.bulk_in;
	RX->direction = DIR_IN;
	RX->size = FRAME_DATA;
	RX->transfer_flags = USB_URB_WAITS;

	ecm.rx_wanted = 0;
	rx_out = 1;
	if (usb_send_urb(RX) != OK) {
		rx_out = 0;
		ecm.rx_retry = 1;
		log_warn(&ecm_log, "a receive could not be asked for\n");
	}
}

/* One packet of a notification, as Linux asks for it. */
static void
submit_note(void)
{
	unsigned size = ecm.info.note_size;

	if (size == 0 || size > NOTE_DATA)
		size = NOTE_DATA;

	memset(note_mem, 0, URB_HEADER);
	NOTE->urb_size = sizeof(note_mem);
	NOTE->dev_id = (int)ecm.dev_id;
	NOTE->type = USB_TRANSFER_INT;
	NOTE->endpoint = (int)ecm.info.note_ep;
	NOTE->direction = DIR_IN;
	NOTE->size = size;

	ecm.note_wanted = 0;
	note_out = 1;
	if (usb_send_urb(NOTE) != OK) {
		note_out = 0;
		ecm.note_retry = 1;
		log_warn(&ecm_log, "a notification could not be asked for\n");
	}
}

/*
 * Whatever is owed, in the one order that keeps the rule: nothing while a
 * control or bulk OUT transfer is out; then the waits, which xhci answers
 * at once; then one control request; then the frames lwip has queued.
 */
static void
kick(void)
{
	if (ctl_out || tx_out)
		return;

	if (ecm.stage == ST_RUNNING) {
		if (ecm.rx_wanted && !rx_out && !ecm.rx_ready)
			submit_rx();
		if (ecm.note_wanted && !note_out && ecm.info.note_ep != 0)
			submit_note();
	}

	if (ecm.ctl_wanted) {
		ecm.ctl_wanted = 0;
		stage_request();
		return;
	}
	if (ecm.stage == ST_RUNNING && ecm.filter_set != mode_filter(nd_mode)) {
		send_filter(mode_filter(nd_mode));
		return;
	}

	if (nd_up)
		netdriver_send();
}

/*===========================================================================*
 *    the device                                                             *
 *===========================================================================*/
static void
set_link(int up)
{
	if (ecm.link == up)
		return;
	ecm.link = up;
	log_info(&ecm_log, "device %u: link %s\n", ecm.dev_id,
	    up ? "up" : "down");
	if (nd_up)
		netdriver_link();
}

static void
forget(const char *why)
{
	if (ecm.stage == ST_NONE)
		return;
	log_info(&ecm_log, "device %u: %s (%lu frame(s) in, %lu out)\n",
	    ecm.dev_id, why, ecm.rx_frames, ecm.tx_frames);
	memset(&ecm, 0, sizeof(ecm));
	ecm.stage = ST_NONE;
	if (nd_up)
		netdriver_link();
}

static void
ecm_connect(unsigned dev_id, unsigned interfaces)
{
	if (ecm.stage != ST_NONE) {
		log_warn(&ecm_log, "device %u is a second CDC Ethernet device; "
		    "only one is driven, and that is device %u\n", dev_id,
		    ecm.dev_id);
		return;
	}

	memset(&ecm, 0, sizeof(ecm));
	ecm.dev_id = dev_id;
	ecm.stage = ST_GETCONF;
	ecm.ctl_wanted = 1;
	ecm.filter_set = ~0u;

	log_info(&ecm_log, "device %u is announced (interfaces 0x%x)\n",
	    dev_id, interfaces);
	kick();
}

static void
ecm_disconnect(unsigned dev_id)
{
	if (ecm.stage != ST_NONE && dev_id == ecm.dev_id)
		forget("withdrawn");
}

/* The data interface is open: frames may go, and lwip may know us. */
static void
opened(void)
{
	ecm.stage = ST_RUNNING;
	ecm.link = 1;		/* as Linux's carrier, until the device says */
	ecm.rx_wanted = 1;
	ecm.note_wanted = ecm.info.note_ep != 0;

	log_info(&ecm_log, "device %u: %s, frames on %u in and %u out (%u "
	    "bytes), %s; link up\n", ecm.dev_id, mac_str(ecm.mac),
	    ecm.info.bulk_in, ecm.info.bulk_out, ecm.info.bulk_out_size,
	    ecm.info.note_ep != 0 ? "notifications on an interrupt endpoint" :
	    "no notifications");

	if (!nd_up) {
		memcpy(nd_mac, ecm.mac, sizeof(nd_mac));
		if (netdriver_init(&ecm_table) != OK) {
			log_warn(&ecm_log, "libnetdriver would not start\n");
			return;
		}
		nd_up = 1;
		log_info(&ecm_log, "interface %s, %s\n", netdriver_name(),
		    mac_str(nd_mac));
	} else
		netdriver_link();
}

/* A control request came back: the next step of setting up. */
static void
ctl_done(struct usb_urb *urb)
{
	const uint8_t *buf = (const uint8_t *)urb->buffer;
	size_t len = urb->actual_length;
	int r;

	ctl_out = 0;

	if (ecm.stage == ST_NONE || (unsigned)urb->dev_id != ecm.dev_id)
		return;			/* for a device that has gone */
	if (urb->status == ENODEV) {
		forget("gone");
		return;
	}

	switch (ecm.stage) {
	case ST_GETCONF:
		/*
		 * Which configuration xhci set - a device with two has CDC
		 * Ethernet in either - so that the descriptor read next is
		 * that one.  Not answered: the first one is read.
		 */
		ecm.config = (urb->status == OK && len >= 1) ? buf[0] : 0;
		ecm.cfg_index = 0;
		ecm.stage = ST_CONFIG;
		ecm.ctl_wanted = 1;
		break;

	case ST_CONFIG:
		if (urb->status == OK && len >= 9 && buf[1] == DT_CONFIG &&
		    (ecm.config == 0 || buf[5] == ecm.config)) {
			r = ecm_parse_config(buf, len, &ecm.info);
			if (r != ECM_OK) {
				log_warn(&ecm_log, "device %u, configuration "
				    "%u: %s\n", ecm.dev_id, buf[5],
				    ecm_parse_error(r));
				forget("not CDC Ethernet after all");
				return;
			}
			log_info(&ecm_log, "device %u, configuration %u: "
			    "control interface %u, data interface %u alt %u, "
			    "station address in string %u, segments of %u\n",
			    ecm.dev_id, buf[5], ecm.info.ctrl_iface,
			    ecm.info.data_iface, ecm.info.data_alt,
			    ecm.info.mac_string, ecm.info.max_segment);
			ecm.stage = ST_LANG;
			ecm.ctl_wanted = 1;
			break;
		}
		if (urb->status != OK || ++ecm.cfg_index >= 8) {
			log_warn(&ecm_log, "device %u: configuration %u not "
			    "found (status %d)\n", ecm.dev_id, ecm.config,
			    urb->status);
			forget("no configuration");
			return;
		}
		ecm.ctl_wanted = 1;		/* the next index */
		break;

	case ST_LANG:
		if (urb->status != OK ||
		    !ecm_parse_langid(buf, len, &ecm.langid))
			ecm.langid = 0x0409;	/* US English, what all have */
		ecm.stage = ST_MAC;
		ecm.ctl_wanted = 1;
		break;

	case ST_MAC:
		if (urb->status != OK || !ecm_parse_mac(buf, len, ecm.mac)) {
			log_warn(&ecm_log, "device %u: no station address in "
			    "string %u (status %d, %u byte(s))\n", ecm.dev_id,
			    ecm.info.mac_string, urb->status, (unsigned)len);
			forget("no station address");
			return;
		}
		if (nd_up && memcmp(ecm.mac, nd_mac, sizeof(nd_mac)) != 0) {
			log_warn(&ecm_log, "device %u is %s, and %s was "
			    "announced as %s; restart usb_ecm to use it\n",
			    ecm.dev_id, mac_str(ecm.mac), netdriver_name(),
			    mac_str(nd_mac));
			forget("another address");
			return;
		}
		ecm.stage = ST_ALT;
		ecm.ctl_wanted = 1;
		break;

	case ST_ALT:
		if (urb->status != OK) {
			log_warn(&ecm_log, "device %u: the data interface would "
			    "not open (status %d)\n", ecm.dev_id, urb->status);
			forget("closed");
			return;
		}
		opened();
		break;

	case ST_RUNNING:
		/*
		 * The packet filter.  Optional for the device, and a stall
		 * means it passes everything anyway - which is what Linux
		 * assumes too - so it is not asked again.
		 */
		if (urb->status != OK)
			log_info(&ecm_log, "device %u did not take packet "
			    "filter 0x%02x (%d); it filters as it likes\n",
			    ecm.dev_id, ecm.filter_set, urb->status);
		else
			log_debug(&ecm_log, "packet filter 0x%02x\n",
			    ecm.filter_set);
		break;

	default:
		break;
	}
}

/* A frame went, or did not. */
static void
tx_done(struct usb_urb *urb)
{
	tx_out = 0;

	if (ecm.stage == ST_NONE || (unsigned)urb->dev_id != ecm.dev_id)
		return;
	if (urb->status == ENODEV) {
		forget("gone");
		return;
	}
	if (urb->status != OK) {
		log_debug(&ecm_log, "a frame of %u byte(s) failed (%d)\n",
		    (unsigned)urb->size, urb->status);
		netdriver_stat_oerror(1);
		return;
	}
	ecm.tx_frames++;
}

/* A frame came, or the receive failed. */
static void
rx_done(struct usb_urb *urb)
{
	size_t len = urb->actual_length;

	rx_out = 0;

	if (ecm.stage == ST_NONE || (unsigned)urb->dev_id != ecm.dev_id)
		return;
	if (urb->status == ENODEV) {
		forget("gone");
		return;
	}
	if (urb->status == ECANCELED)
		return;			/* the interface changed under it */
	if (urb->status != OK) {
		/* What usb_kbd does: a pause, and give up after fifty. */
		if (++ecm.rx_errors >= MAX_ERRORS) {
			forget("too many failed receives");
			return;
		}
		log_debug(&ecm_log, "a receive failed (%d), %u in a row\n",
		    urb->status, ecm.rx_errors);
		netdriver_stat_ierror(1);
		ecm.rx_retry = 1;
		return;
	}
	ecm.rx_errors = 0;

	/*
	 * Nothing but a zero-length packet is not a frame, and not an error
	 * either; anything shorter than a header or longer than a tagged
	 * frame is.
	 */
	if (len == 0) {
		ecm.rx_wanted = 1;
		return;
	}
	if (len < 14 || len > NDEV_ETH_PACKET_MAX_TAGGED) {
		ecm.rx_bad++;
		netdriver_stat_ierror(1);
		ecm.rx_wanted = 1;
		return;
	}

	ecm.rx_len = len;
	ecm.rx_ready = 1;
	if (nd_up)
		netdriver_recv();
}

/* A notification, or a part of one. */
static void
note_done(struct usb_urb *urb)
{
	struct ecm_note note;

	note_out = 0;

	if (ecm.stage == ST_NONE || (unsigned)urb->dev_id != ecm.dev_id)
		return;
	if (urb->status == ENODEV) {
		forget("gone");
		return;
	}
	if (urb->status == ECANCELED)
		return;
	if (urb->status != OK) {
		if (++ecm.note_errors >= MAX_ERRORS) {
			log_warn(&ecm_log, "device %u: notifications keep "
			    "failing (%d); the link is left as it is\n",
			    ecm.dev_id, urb->status);
			return;
		}
		ecm.note_retry = 1;
		return;
	}
	ecm.note_errors = 0;

	if (ecm_note_feed(&ecm.notes, (const uint8_t *)urb->buffer,
	    urb->actual_length, &note)) {
		if (note.what == ECM_NOTE_CONNECTION)
			set_link(note.connected != 0);
		else if (note.what == ECM_NOTE_SPEED) {
			if (note.down != ecm.speed)
				log_info(&ecm_log, "device %u: %u kbit/s down, "
				    "%u up\n", ecm.dev_id,
				    (unsigned)(note.down / 1000),
				    (unsigned)(note.up / 1000));
			ecm.speed = note.down;
			if (nd_up)
				netdriver_link();
		}
	}
	ecm.note_wanted = 1;
}

static void
ecm_urb_completion(struct usb_urb *urb)
{
	if (urb == CTL)
		ctl_done(urb);
	else if (urb == TX)
		tx_done(urb);
	else if (urb == RX)
		rx_done(urb);
	else if (urb == NOTE)
		note_done(urb);
	else
		log_warn(&ecm_log, "a completion for a block that is not "
		    "ours\n");
	kick();
}

/*===========================================================================*
 *    libnetdriver                                                           *
 *===========================================================================*/
static int
ecm_init(unsigned int instance, netdriver_addr_t *hwaddr, uint32_t *caps,
	unsigned int *ticks)
{
	memcpy(hwaddr->na_addr, nd_mac, sizeof(nd_mac));
	*caps = 0;
	*ticks = sys_hz() / 10 != 0 ? sys_hz() / 10 : 1;
	return OK;
}

static void
ecm_set_mode(unsigned int mode, const netdriver_addr_t *mcast_list,
	unsigned int mcast_count)
{
	nd_mode = mode;
	kick();
}

static ssize_t
ecm_recv(struct netdriver_data *data, size_t max)
{
	size_t len = ecm.rx_len;

	if (!ecm.rx_ready)
		return SUSPEND;
	ecm.rx_ready = 0;
	ecm.rx_wanted = 1;

	if (len > max) {
		ecm.rx_bad++;
		netdriver_stat_ierror(1);
		kick();
		return SUSPEND;
	}
	if (len < NDEV_ETH_PACKET_MIN) {
		memset(RX->buffer + len, 0, NDEV_ETH_PACKET_MIN - len);
		len = NDEV_ETH_PACKET_MIN;
	}
	netdriver_copyout(data, 0, RX->buffer, len);
	ecm.rx_frames++;
	kick();
	return (ssize_t)len;
}

static int
ecm_send(struct netdriver_data *data, size_t size)
{
	size_t len;

	if (ecm.stage != ST_RUNNING || !ecm.link) {	/* nobody to give it to */
		ecm.tx_dropped++;
		netdriver_stat_oerror(1);
		return OK;
	}
	if (size + 1 > FRAME_DATA)
		return EINVAL;
	if (ctl_out || tx_out)
		return SUSPEND;		/* kick() asks again */

	/* The header first: sizeof() of the block covers buffer[0] too. */
	memset(tx_mem, 0, URB_HEADER);
	netdriver_copyin(data, 0, TX->buffer, size);
	len = ecm_tx_length(size, ecm.info.bulk_out_size);
	if (len > size)
		TX->buffer[size] = 0;

	TX->urb_size = USB_URBSIZE(len, 0);
	TX->dev_id = (int)ecm.dev_id;
	TX->type = USB_TRANSFER_BLK;
	TX->endpoint = (int)ecm.info.bulk_out;
	TX->direction = DIR_OUT;
	TX->size = len;

	tx_out = 1;
	if (usb_send_urb(TX) != OK) {
		tx_out = 0;
		netdriver_stat_oerror(1);
	}
	return OK;
}

static unsigned int
ecm_get_link(uint32_t *media)
{
	if (ecm.stage != ST_RUNNING || !ecm.link)
		return NDEV_LINK_DOWN;

	if (ecm.speed >= 1000000000)
		*media = IFM_ETHER | IFM_FDX | IFM_1000_T;
	else if (ecm.speed >= 100000000)
		*media = IFM_ETHER | IFM_FDX | IFM_100_TX;
	else if (ecm.speed != 0)
		*media = IFM_ETHER | IFM_FDX | IFM_10_T;
	else
		*media = IFM_ETHER | IFM_AUTO;
	return NDEV_LINK_UP;
}

/* Every tenth of a second: what failed is asked for again. */
static void
ecm_tick(void)
{
	if (ecm.rx_retry) {
		ecm.rx_retry = 0;
		ecm.rx_wanted = 1;
	}
	if (ecm.note_retry) {
		ecm.note_retry = 0;
		ecm.note_wanted = 1;
	}
	kick();
}

static const struct netdriver ecm_table = {
	.ndr_name	= "cdce",
	.ndr_init	= ecm_init,
	.ndr_set_mode	= ecm_set_mode,
	.ndr_recv	= ecm_recv,
	.ndr_send	= ecm_send,
	.ndr_get_link	= ecm_get_link,
	.ndr_tick	= ecm_tick
};

/*===========================================================================*
 *    start, stop and the loop                                               *
 *===========================================================================*/
static int
ecm_sef_init(int type, sef_init_info_t *info)
{
	static char name[] = "usb_ecm";		/* libusb takes no const */
	long v;
	int r;

	if (env_parse("log", "d", 0, &v, LEVEL_NONE, LEVEL_TRACE) == EP_SET)
		ecm_log.log_level = (int)v;

	memset(&ecm, 0, sizeof(ecm));
	ecm.stage = ST_NONE;

	/*
	 * Refused politely when the controller is not there: libusb's
	 * usb_init() panics, and RS would restart the panic for ever.
	 */
	if ((r = ds_retrieve_label_endpt("usbd", &usbd_ep)) != OK) {
		log_warn(&ecm_log, "no USB host controller is running (no "
		    "\"usbd\": %d)\n", r);
		return r;
	}
	usb_init(name);

	(void)sys_setalarm(sys_hz() / 10, FALSE);
	log_info(&ecm_log, "waiting for a CDC Ethernet device\n");
	return OK;
}

/*
 * On the way out: tell xhci this driver is gone, so that what waits on its
 * rings is answered to nobody - unless a transfer whose completion xhci
 * sends is out, when the request would take that completion for its answer.
 */
static void
ecm_signal(int signo)
{
	message m;

	if (signo != SIGTERM)
		return;

	if (usbd_ep != NONE && !ctl_out && !tx_out) {
		memset(&m, 0, sizeof(m));
		m.m_type = USB_RQ_DEINIT;
		(void)ipc_sendrec(usbd_ep, &m);
	}

	running = 0;
	if (nd_up)
		netdriver_terminate();
	else
		sef_cancel();
}

int
main(int argc, char *argv[])
{
	message m;
	int r, ipc_status;

	/* Without this env_parse() sees none of the -args. */
	env_setargs(argc, argv);

	sef_setcb_init_fresh(ecm_sef_init);
	sef_setcb_signal_handler(ecm_signal);
	sef_startup();

	while (running) {
		if ((r = sef_receive_status(ANY, &m, &ipc_status)) != OK) {
			if (r == EINTR)
				continue;	/* sef_cancel() was called */
			panic("usb_ecm: sef_receive_status failed: %d", r);
		}

		/* The USB protocol, from xhci. */
		if (usbd_ep != NONE && m.m_source == usbd_ep &&
		    !is_ipc_notify(ipc_status)) {
			if (m.m_type == USB_COMPLETE_URB ||
			    m.m_type == USB_ANNOUCE_DEV ||
			    m.m_type == USB_WITHDRAW_DEV)
				usb_handle_msg(&ecm_usb, &m);
			else
				log_warn(&ecm_log, "unexpected message 0x%x "
				    "from xhci\n", m.m_type);
			continue;
		}

		/* lwip, and the ticks, once lwip knows this driver. */
		if (nd_up) {
			netdriver_process(&ecm_table, &m, ipc_status);
			continue;
		}

		/* Before that, the ticks are this loop's. */
		if (is_ipc_notify(ipc_status) && m.m_source == CLOCK) {
			ecm_tick();
			if (!nd_up)
				(void)sys_setalarm(sys_hz() / 10, FALSE);
			continue;
		}

		log_debug(&ecm_log, "message 0x%x from %d before there is an "
		    "interface\n", m.m_type, m.m_source);
	}

	return 0;
}
