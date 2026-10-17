/*
 * usb_ecm stand: the CDC Ethernet host driver, whole, against a model of
 * the host controller's URB protocol and of the devices on the far side.
 *
 * usb_ecm.c is included as it is (main renamed) and built with the host's
 * compiler against the headers in stubs/; ecm_desc.c beside it.  The stand
 * plays three parts:
 *
 *   xhci        the URB protocol as xhci_urb.c answers it, and its rules -
 *               each one a way the real thing fails, written as a
 *               violation the stand counts: a request while the sent
 *               completion of a control or bulk OUT transfer is owed (the
 *               driver would take that completion for its answer, and
 *               libusb panics); a bulk IN without USB_URB_WAITS (it holds
 *               every other transfer behind it, then fails in five
 *               seconds); a second waiting transfer on one endpoint
 *               (EBUSY: a panic); a transfer on an endpoint the device
 *               does not have in its current alternate setting (EINVAL: a
 *               panic); a block too small for its data.  Waiting
 *               transfers end the way the controller ends them - a frame,
 *               or packets up to a short one - and are cancelled with
 *               ECANCELED when their interface changes its setting
 *   the device  answers control requests from its descriptors: the
 *               Mi Pad 4's function byte for byte (dwc3_ecm/ecm.c), and an
 *               adapter on an RTL8153 with CDC Ethernet in its second
 *               configuration and notifications in packets of eight; it
 *               sends frames and notifications when told to, keeps the
 *               frames it is sent, and holds a frame that ends on a whole
 *               packet against the driver - its receiver would wait for the
 *               rest
 *   lwip        libnetdriver's calls: receive requests, frames to send,
 *               the link as the driver reports it
 *
 *   sh run.sh          the checks, quiet
 *   sh run.sh -v       every check and the driver's own logging
 */

#define main ecm_main
#include "usb_ecm.c"
#undef main

#include <stdarg.h>

static int verbose;
static unsigned checks, failures;

static void
check(int ok, const char *fmt, ...)
{
	va_list ap;

	checks++;
	if (ok && !verbose)
		return;
	if (!ok)
		failures++;
	va_start(ap, fmt);
	printf("%s ", ok ? "ok  " : "FAIL");
	vprintf(fmt, ap);
	printf("\n");
	va_end(ap);
}

static void
section(const char *name)
{
	printf("-- %s\n", name);
}

/*===========================================================================*
 *    the system around the driver                                           *
 *===========================================================================*/
static unsigned n_warn;
static char last_warn[512];

void
default_log(struct log *l, int level, const char *fmt, ...)
{
	char buf[512];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (level == LEVEL_WARN) {
		n_warn++;
		snprintf(last_warn, sizeof(last_warn), "%s", buf);
	}
	if (verbose && level <= LEVEL_DEBUG)
		printf("  %s: %s", l->name, buf);
}

int env_parse(const char *env, const char *fmt, int field, long *param,
	long min, long max) { return 0; }
void env_setargs(int argc, char **argv) { }
void sef_setcb_init_fresh(int (*cb)(int, sef_init_info_t *)) { }
void sef_setcb_signal_handler(void (*cb)(int)) { }
void sef_startup(void) { }
static unsigned sef_cancels, alarms;
void sef_cancel(void) { sef_cancels++; }
int sef_receive_status(endpoint_t src, message *m, int *status)
{ return EINTR; }
int sys_setalarm(clock_t ticks, int abs_time) { alarms++; return OK; }
clock_t sys_hz(void) { return 100; }

void
panic(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	printf("PANIC: ");
	vprintf(fmt, ap);
	printf("\n");
	va_end(ap);
	exit(2);
}

int
ds_retrieve_label_endpt(const char *label, endpoint_t *ep)
{
	if (strcmp(label, "usbd") != 0)
		return ESRCH;
	*ep = 77;
	return OK;
}

/*===========================================================================*
 *    the device                                                             *
 *===========================================================================*/
/* The Mi Pad 4's function, from minix/drivers/net/dwc3_ecm/ecm.c. */
static const uint8_t tab_cfg[80] = {
	9, 2, 80, 0, 2, 1, 0, 0xc0, 250,
	9, 4, 0, 0, 1, 0x02, 0x06, 0x00, 0,
	5, 0x24, 0x00, 0x10, 0x01,
	5, 0x24, 0x06, 0, 1,
	13, 0x24, 0x0f, 3, 0, 0, 0, 0, 0xea, 0x05, 0, 0, 0,
	7, 5, 0x82, 3, 16, 0, 9,
	9, 4, 1, 0, 0, 0x0a, 0, 0, 0,
	9, 4, 1, 1, 2, 0x0a, 0, 0, 0,
	7, 5, 0x81, 2, 0x00, 0x02, 0,
	7, 5, 0x01, 2, 0x00, 0x02, 0
};

/* An RTL8153: its own protocol first, CDC Ethernet second. */
static const uint8_t rtl_cfg0[39] = {
	9, 2, 39, 0, 1, 1, 0, 0xa0, 50,
	9, 4, 0, 0, 3, 0xff, 0xff, 0x00, 0,
	7, 5, 0x81, 2, 0x00, 0x02, 0,
	7, 5, 0x02, 2, 0x00, 0x02, 0,
	7, 5, 0x83, 3, 2, 0, 8
};

static const uint8_t rtl_cfg1[80] = {
	9, 2, 80, 0, 2, 2, 0, 0xa0, 50,
	9, 4, 0, 0, 1, 0x02, 0x06, 0x00, 5,
	5, 0x24, 0x00, 0x10, 0x01,
	5, 0x24, 0x06, 0, 1,
	13, 0x24, 0x0f, 6, 0, 0, 0, 0, 0xea, 0x05, 0, 0, 0,
	7, 5, 0x83, 3, 8, 0, 8,
	9, 4, 1, 0, 0, 0x0a, 0, 0, 0,
	9, 4, 1, 1, 2, 0x0a, 0, 0, 0,
	7, 5, 0x81, 2, 0x00, 0x02, 0,
	7, 5, 0x02, 2, 0x00, 0x02, 0
};

/*
 * The user's dock's RTL8152 (0bda:8152 2000), byte for byte as it answered
 * the board's Linux (port/cb2-usb/dock-reference.txt): Realtek's own first,
 * CDC Ethernet second - notifications on 3 in, sixteen bytes in a packet,
 * the station address in string 3 and in capitals.
 */
static const uint8_t rtl8152_cfg0[39] = {
	9, 2, 39, 0, 1, 1, 0, 0xa0, 50,
	9, 4, 0, 0, 3, 0xff, 0xff, 0x00, 0,
	7, 5, 0x81, 2, 0x00, 0x02, 0,
	7, 5, 0x02, 2, 0x00, 0x02, 0,
	7, 5, 0x83, 3, 0x02, 0x00, 8
};

static const uint8_t rtl8152_cfg1[80] = {
	9, 2, 80, 0, 2, 2, 0, 0xa0, 50,
	9, 4, 0, 0, 1, 0x02, 0x06, 0x00, 5,
	5, 0x24, 0x00, 0x10, 0x01,
	5, 0x24, 0x06, 0, 1,
	13, 0x24, 0x0f, 3, 0, 0, 0, 0, 0xea, 0x05, 0, 0, 0,
	7, 5, 0x83, 3, 0x10, 0x00, 8,
	9, 4, 1, 0, 0, 0x0a, 0, 0, 0,
	9, 4, 1, 1, 2, 0x0a, 0, 0, 4,
	7, 5, 0x81, 2, 0x00, 0x02, 0,
	7, 5, 0x02, 2, 0x00, 0x02, 0
};

struct mdev {
	const char *name;
	const uint8_t *cfg[2];
	size_t cfg_len[2];
	unsigned ncfg;
	unsigned config;		/* the value xhci set */
	const char *mac;		/* string iMACAddress, as answered */
	unsigned mac_index;
	unsigned ctrl_iface, data_iface, data_alt;
	unsigned bulk_in, bulk_out, note_ep;
	unsigned note_max, bulk_max;
	int notify_on_open;		/* the tablet: news when alt 1 is set */
};

static const struct mdev tablet = {
	"the Mi Pad 4", { tab_cfg, NULL }, { sizeof(tab_cfg), 0 }, 1, 1,
	"064D4543484F", 3, 0, 1, 1, 1, 1, 2, 16, 512, 1
};

static const struct mdev tablet_other = {
	"another tablet", { tab_cfg, NULL }, { sizeof(tab_cfg), 0 }, 1, 1,
	"064D4543484E", 3, 0, 1, 1, 1, 1, 2, 16, 512, 1
};

static const struct mdev rtl = {
	"an RTL8153", { rtl_cfg0, rtl_cfg1 },
	{ sizeof(rtl_cfg0), sizeof(rtl_cfg1) }, 2, 2,
	"00e04c36a1b2", 6, 0, 1, 1, 1, 2, 3, 8, 512, 0
};

static const struct mdev rtl8152 = {
	"the dock's RTL8152", { rtl8152_cfg0, rtl8152_cfg1 },
	{ sizeof(rtl8152_cfg0), sizeof(rtl8152_cfg1) }, 2, 2,
	"00E04C367012", 3, 0, 1, 1, 1, 2, 3, 16, 512, 0
};

#define MAX_FRAMES	64

struct frame {
	uint8_t b[4096];
	size_t len;
};

static struct {
	const struct mdev *d;
	unsigned id;
	int present;
	unsigned alt;
	unsigned filter, filters, filter_stalls;
	int stall_filter, stall_alt;
	struct frame in[MAX_FRAMES];		/* what it will send */
	unsigned in_head, in_n;
	struct frame note[16];
	unsigned note_head, note_n;
	struct frame out[MAX_FRAMES];		/* what it was sent */
	unsigned out_n;
	char req[64][40];			/* the requests, in order */
	unsigned nreq, unexpected;
} dv;

static const uint8_t *cur_cfg_override;	/* a broken configuration */
static size_t cur_cfg_override_len;

static size_t
mac_string(uint8_t *out, size_t max, const char *s)
{
	size_t n = strlen(s), i;

	if (2 + 2 * n > max)
		n = (max - 2) / 2;
	out[0] = (uint8_t)(2 + 2 * n);
	out[1] = 3;
	for (i = 0; i < n; i++) {
		out[2 + 2 * i] = (uint8_t)s[i];
		out[3 + 2 * i] = 0;
	}
	return 2 + 2 * n;
}

static void
dev_note(const uint8_t *b, size_t len)
{
	struct frame *f = &dv.note[(dv.note_head + dv.note_n) % 16];

	memcpy(f->b, b, len);
	f->len = len;
	dv.note_n++;
}

static void
note_connection(unsigned up)
{
	uint8_t n[8] = { 0xa1, 0x00, (uint8_t)up, 0, 0, 0, 0, 0 };

	dev_note(n, sizeof(n));
}

static void
put32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

/* SPEED_CHANGE: one packet of sixteen, or two of eight. */
static void
note_speed(uint32_t down, uint32_t up, unsigned max_packet)
{
	uint8_t n[16] = { 0xa1, 0x2a, 0, 0, 0, 0, 8, 0 };

	put32(n + 8, down);
	put32(n + 12, up);
	if (max_packet >= 16)
		dev_note(n, 16);
	else {
		dev_note(n, 8);
		dev_note(n + 8, 8);
	}
}

static void
dev_send(const uint8_t *b, size_t len)
{
	struct frame *f = &dv.in[(dv.in_head + dv.in_n) % MAX_FRAMES];

	memcpy(f->b, b, len);
	f->len = len;
	dv.in_n++;
}

/* The device's side of a control request. */
static int
dev_control(const struct usb_ctrlrequest *rq, uint8_t *buf, size_t max,
	size_t *actual)
{
	const struct mdev *d = dv.d;
	unsigned type = rq->bRequestType, r = rq->bRequest;
	unsigned v = rq->wValue, ix = rq->wIndex;
	const uint8_t *cfg;
	size_t n;

	if (dv.nreq < 64)
		snprintf(dv.req[dv.nreq], sizeof(dv.req[0]), "%02x %02x %04x "
		    "%u %u", type, r, v, ix, rq->wLength);
	dv.nreq++;
	*actual = 0;

	if (type == 0x80 && r == 8 && max >= 1) {
		buf[0] = (uint8_t)d->config;
		*actual = 1;
		return OK;
	}
	if (type == 0x80 && r == 6) {
		if ((v >> 8) == 2) {
			if ((v & 0xff) >= d->ncfg)
				return EIO;
			cfg = d->cfg[v & 0xff];
			n = d->cfg_len[v & 0xff];
			if (cur_cfg_override != NULL &&
			    cfg[5] == d->config) {
				cfg = cur_cfg_override;
				n = cur_cfg_override_len;
			}
			if (n > max)
				n = max;
			memcpy(buf, cfg, n);
			*actual = n;
			return OK;
		}
		if ((v >> 8) == 3 && (v & 0xff) == 0 && max >= 4) {
			buf[0] = 4;
			buf[1] = 3;
			buf[2] = 0x09;
			buf[3] = 0x04;
			*actual = 4;
			return OK;
		}
		if ((v >> 8) == 3 && (v & 0xff) == d->mac_index) {
			if (ix != 0x0409)
				return EIO;	/* a language it has not */
			*actual = mac_string(buf, max, d->mac);
			return OK;
		}
		return EIO;
	}
	if (type == 0x01 && r == 11) {
		if (ix == d->data_iface && (v == 0 || v == d->data_alt) &&
		    !dv.stall_alt) {
			dv.alt = v;
			if (v == d->data_alt && d->notify_on_open) {
				note_connection(1);
				note_speed(13u * 512 * 8 * 1000 * 8,
				    13u * 512 * 8 * 1000 * 8, d->note_max);
			}
			return OK;
		}
		if (ix == d->ctrl_iface && v == 0)
			return OK;
		return EIO;
	}
	if (type == 0x21 && r == 0x43) {
		if (ix != d->ctrl_iface || dv.stall_filter) {
			dv.filter_stalls++;
			return EIO;
		}
		dv.filter = v;
		dv.filters++;
		return OK;
	}
	dv.unexpected++;
	return EIO;
}

/*===========================================================================*
 *    xhci                                                                   *
 *===========================================================================*/
#define MAX_WAIT	8

static struct {
	struct usb_urb *sync;		/* control or bulk OUT, completion sent */
	struct usb_urb *wait[MAX_WAIT];	/* on the rings */
	struct { struct usb_urb *urb; int status; } posted[MAX_WAIT];
	unsigned nposted;
	unsigned next_id, submits, deinits;
	unsigned violations;
	char violation[256];
	const char *client;
} hc;

static void
violation(const char *fmt, ...)
{
	va_list ap;

	if (hc.violations++ == 0) {
		va_start(ap, fmt);
		vsnprintf(hc.violation, sizeof(hc.violation), fmt, ap);
		va_end(ap);
	}
	if (verbose) {
		va_start(ap, fmt);
		printf("  xhci: VIOLATION: ");
		vprintf(fmt, ap);
		printf("\n");
		va_end(ap);
	}
}

static int
ep_open(int ep, int in, int bulk)
{
	const struct mdev *d = dv.d;

	if (bulk)
		return dv.alt == d->data_alt &&
		    (unsigned)ep == (in ? d->bulk_in : d->bulk_out);
	return in && (unsigned)ep == d->note_ep;
}

static void
post(struct usb_urb *urb, int status)
{
	if (hc.nposted < MAX_WAIT) {
		hc.posted[hc.nposted].urb = urb;
		hc.posted[hc.nposted].status = status;
		hc.nposted++;
	}
}

int
usb_init(char *name)
{
	hc.client = name;
	return 0;
}

int
usb_handle_msg(struct usb_driver *ubd, message *msg)
{
	return 0;
}

int
ipc_sendrec(endpoint_t dst, message *m)
{
	if (hc.sync != NULL)
		violation("a message to xhci while a completion is owed");
	if (m->m_type == USB_RQ_DEINIT)
		hc.deinits++;
	return OK;
}

int
usb_send_urb(struct usb_urb *urb)
{
	struct usb_ctrlrequest *rq;
	unsigned i, free_slot = MAX_WAIT;

	hc.submits++;
	if (hc.sync != NULL)
		violation("a request while the completion of the last is owed: "
		    "it would be taken for the answer");
	if (urb->urb_size < USB_URBSIZE(urb->size, 0))
		violation("a block of %zu bytes for %zu bytes of data",
		    urb->urb_size, urb->size);
	urb->urb_id = ++hc.next_id;

	/* A device that has gone: taken, and failed with ENODEV. */
	if (!dv.present || (unsigned)urb->dev_id != dv.id) {
		post(urb, ENODEV);
		return OK;
	}

	switch (urb->type) {
	case USB_TRANSFER_CTL:
		rq = (struct usb_ctrlrequest *)urb->setup_packet;
		if (urb->direction != ((rq->bRequestType & 0x80) ? 1 : 0))
			violation("a control request with direction %d",
			    urb->direction);
		if (rq->wLength > 4096 || rq->wLength > urb->size)
			violation("a control request for %u bytes in a block "
			    "of %zu", rq->wLength, urb->size);
		/* xhci's waits_cancel(): the interface's waits go first. */
		if (rq->bRequestType == 0x01 && rq->bRequest == 11)
			for (i = 0; i < MAX_WAIT; i++)
				if (hc.wait[i] != NULL &&
				    hc.wait[i]->type == USB_TRANSFER_BLK &&
				    rq->wIndex == dv.d->data_iface) {
					post(hc.wait[i], ECANCELED);
					hc.wait[i] = NULL;
				}
		hc.sync = urb;
		return OK;

	case USB_TRANSFER_BLK:
		if (urb->direction != 1) {
			if (!ep_open(urb->endpoint, 0, 1)) {
				violation("bulk OUT on endpoint %d, which the "
				    "device does not have in alternate %u",
				    urb->endpoint, dv.alt);
				return OK;
			}
			hc.sync = urb;
			return OK;
		}
		if (!(urb->transfer_flags & USB_URB_WAITS))
			violation("a bulk IN without USB_URB_WAITS: it would "
			    "hold the controller until a frame came");
		if (!ep_open(urb->endpoint, 1, 1)) {
			violation("bulk IN on endpoint %d, which the device "
			    "does not have in alternate %u", urb->endpoint,
			    dv.alt);
			return OK;
		}
		break;

	case USB_TRANSFER_INT:
		if (urb->direction != 1 || !ep_open(urb->endpoint, 1, 0)) {
			violation("an interrupt transfer on endpoint %d %s",
			    urb->endpoint, urb->direction == 1 ? "in" : "out");
			return OK;
		}
		break;

	default:
		violation("a transfer of type %d", urb->type);
		return OK;
	}

	if (urb->size == 0 || urb->size > 2048)
		violation("a waiting transfer of %zu bytes", urb->size);
	for (i = 0; i < MAX_WAIT; i++) {
		if (hc.wait[i] == NULL) {
			if (free_slot == MAX_WAIT)
				free_slot = i;
			continue;
		}
		if (hc.wait[i]->type == urb->type &&
		    hc.wait[i]->endpoint == urb->endpoint) {
			violation("a second transfer on endpoint %d: EBUSY",
			    urb->endpoint);
			return OK;
		}
	}
	if (free_slot < MAX_WAIT)
		hc.wait[free_slot] = urb;
	return OK;
}

static void
complete(struct usb_urb *urb, int status, size_t actual)
{
	urb->status = status;
	urb->actual_length = status == OK ? actual : 0;
	ecm_urb_completion(urb);
}

/* The transfer that is out: the device answers it. */
static void
run_sync(struct usb_urb *u)
{
	struct usb_ctrlrequest *rq = (struct usb_ctrlrequest *)u->setup_packet;
	size_t actual = 0, max;
	int status;

	if (u->type == USB_TRANSFER_CTL) {
		max = rq->wLength < u->size ? rq->wLength : u->size;
		status = dev_control(rq, (uint8_t *)u->buffer, max, &actual);
		complete(u, status, actual);
		return;
	}

	/* A frame out: the device's receiver ends it on a short packet. */
	if (u->size % dv.d->bulk_max == 0)
		violation("a frame of %zu bytes ends on a whole packet: the "
		    "device waits for the rest of it", u->size);
	if (dv.out_n < MAX_FRAMES) {
		memcpy(dv.out[dv.out_n].b, u->buffer, u->size);
		dv.out[dv.out_n].len = u->size;
		dv.out_n++;
	}
	complete(u, OK, u->size);
}

/* A waiting transfer, if the device has what it waits for. */
static int
run_wait(unsigned i)
{
	struct usb_urb *w = hc.wait[i];
	struct frame *f;
	size_t total = 0, n;
	unsigned k, take = 0;
	int ends = 0;

	if (w->type == USB_TRANSFER_BLK) {
		if (dv.alt != dv.d->data_alt || dv.in_n == 0)
			return 0;
		f = &dv.in[dv.in_head];
		dv.in_head = (dv.in_head + 1) % MAX_FRAMES;
		dv.in_n--;
		hc.wait[i] = NULL;
		if (f->len > w->size) {
			complete(w, EIO, 0);		/* babble */
			return 1;
		}
		memcpy(w->buffer, f->b, f->len);
		complete(w, OK, f->len);
		return 1;
	}

	/* Packets up to a short one, or up to what was asked: xHCI's TD. */
	for (k = 0; k < dv.note_n; k++) {
		f = &dv.note[(dv.note_head + k) % 16];
		total += f->len;
		take++;
		if (f->len < dv.d->note_max || total >= w->size) {
			ends = 1;
			break;
		}
	}
	if (!ends)
		return 0;
	hc.wait[i] = NULL;
	total = 0;
	for (k = 0; k < take; k++) {
		f = &dv.note[dv.note_head];
		n = f->len;
		if (total + n > w->size)
			n = w->size - total;
		memcpy(w->buffer + total, f->b, n);
		total += n;
		dv.note_head = (dv.note_head + 1) % 16;
		dv.note_n--;
	}
	complete(w, OK, total);
	return 1;
}

static unsigned steps;

static void
step(void)
{
	struct usb_urb *u;
	unsigned i;
	int status;

	while (hc.nposted > 0) {
		u = hc.posted[0].urb;
		status = hc.posted[0].status;
		for (i = 1; i < hc.nposted; i++)
			hc.posted[i - 1] = hc.posted[i];
		hc.nposted--;
		complete(u, status, 0);
	}
	if (hc.sync != NULL) {
		u = hc.sync;
		hc.sync = NULL;
		run_sync(u);
	}
	for (i = 0; i < MAX_WAIT; i++)
		if (hc.wait[i] != NULL)
			(void)run_wait(i);

	if (++steps % 10 == 0)
		ecm_tick();
}

static void
run(unsigned n)
{
	while (n-- > 0)
		step();
}

static unsigned
waits_of(int type)
{
	unsigned i, n = 0;

	for (i = 0; i < MAX_WAIT; i++)
		if (hc.wait[i] != NULL && hc.wait[i]->type == type)
			n++;
	return n;
}

static struct usb_urb *
wait_of(int type)
{
	unsigned i;

	for (i = 0; i < MAX_WAIT; i++)
		if (hc.wait[i] != NULL && hc.wait[i]->type == type)
			return hc.wait[i];
	return NULL;
}

/* The device goes: what waits fails with ENODEV, what is out with EIO. */
static void
dev_gone(void)
{
	unsigned i;

	dv.present = 0;
	for (i = 0; i < MAX_WAIT; i++)
		if (hc.wait[i] != NULL) {
			post(hc.wait[i], ENODEV);
			hc.wait[i] = NULL;
		}
	if (hc.sync != NULL) {
		post(hc.sync, EIO);
		hc.sync = NULL;
	}
	run(20);
}

static void
dev_arrive(const struct mdev *d, unsigned id)
{
	memset(&dv, 0, sizeof(dv));
	dv.d = d;
	dv.id = id;
	dv.present = 1;
	ecm_usb.connect_device(id, 0x3);
	run(40);
}

/*===========================================================================*
 *    lwip, through libnetdriver                                             *
 *===========================================================================*/
static struct {
	unsigned announced, links, link_state, sent;
	uint32_t media;
	uint8_t hwaddr[6];
	unsigned recv_want;
	struct frame rx[MAX_FRAMES];
	unsigned rx_n;
	struct frame tx[MAX_FRAMES];
	unsigned tx_head, tx_n;
	unsigned oerrors, ierrors, terminated;
	int in_recv, in_send;
} lw;

int
netdriver_init(const struct netdriver *ndp)
{
	netdriver_addr_t a;
	uint32_t caps;
	unsigned ticks;
	int r;

	lw.announced++;
	r = ndp->ndr_init(0, &a, &caps, &ticks);
	memcpy(lw.hwaddr, a.na_addr, 6);
	/* lwip's NDEV_INIT follows, and do_init() answers it with the link. */
	lw.link_state = ndp->ndr_get_link(&lw.media);
	return r;
}

void netdriver_process(const struct netdriver *ndp, const message *m_ptr,
	int ipc_status) { }
void netdriver_terminate(void) { lw.terminated++; sef_cancel(); }
const char *netdriver_name(void) { return "cdce0"; }
void netdriver_stat_oerror(uint32_t count) { lw.oerrors += count; }
void netdriver_stat_ierror(uint32_t count) { lw.ierrors += count; }

void
netdriver_link(void)
{
	lw.links++;
	lw.link_state = ecm_table.ndr_get_link(&lw.media);
}

void
netdriver_copyin(struct netdriver_data *data, size_t off, void *ptr,
	size_t size)
{
	memcpy(ptr, data->buf + off, size);
}

void
netdriver_copyout(struct netdriver_data *data, size_t off, const void *ptr,
	size_t size)
{
	memcpy(data->buf + off, ptr, size);
	if (off + size > data->len)
		data->len = off + size;
}

void
netdriver_recv(void)
{
	static struct netdriver_data d;
	ssize_t r;

	if (lw.in_recv)
		return;
	lw.in_recv = 1;
	while (lw.recv_want > 0) {
		memset(&d, 0xee, sizeof(d));
		d.len = 0;
		r = ecm_table.ndr_recv(&d, NDEV_ETH_PACKET_MAX_TAGGED);
		if (r == SUSPEND)
			break;
		check(r > 0 && (size_t)r == d.len, "a frame handed up says %zd "
		    "bytes and has %zu", r, d.len);
		if (lw.rx_n < MAX_FRAMES) {
			memcpy(lw.rx[lw.rx_n].b, d.buf, d.len);
			lw.rx[lw.rx_n].len = (size_t)r;
			lw.rx_n++;
		}
		lw.recv_want--;
	}
	lw.in_recv = 0;
}

void
netdriver_send(void)
{
	static struct netdriver_data d;
	struct frame *f;
	int r;

	if (lw.in_send)
		return;
	lw.in_send = 1;
	while (lw.tx_n > 0) {
		f = &lw.tx[lw.tx_head];
		memcpy(d.buf, f->b, f->len);
		d.len = f->len;
		r = ecm_table.ndr_send(&d, f->len);
		if (r == SUSPEND)
			break;
		lw.tx_head = (lw.tx_head + 1) % MAX_FRAMES;
		lw.tx_n--;
		lw.sent++;
	}
	lw.in_send = 0;
}

static void
lw_want(unsigned n)
{
	lw.recv_want += n;
	netdriver_recv();
}

static void
lw_send(const uint8_t *b, size_t len)
{
	struct frame *f = &lw.tx[(lw.tx_head + lw.tx_n) % MAX_FRAMES];

	memcpy(f->b, b, len);
	f->len = len;
	lw.tx_n++;
	netdriver_send();
}

/*===========================================================================*
 *    a fresh driver                                                         *
 *===========================================================================*/
static void
driver_fresh(void)
{
	memset(&hc, 0, sizeof(hc));
	memset(&dv, 0, sizeof(dv));
	memset(&lw, 0, sizeof(lw));
	ctl_out = tx_out = rx_out = note_out = 0;
	usbd_ep = NONE;
	nd_up = 0;
	memset(nd_mac, 0, sizeof(nd_mac));
	nd_mode = NDEV_MODE_DOWN;
	running = 1;
	cur_cfg_override = NULL;
	n_warn = 0;
	last_warn[0] = '\0';
	check(ecm_sef_init(0, NULL) == OK && hc.client != NULL &&
	    strcmp(hc.client, "usb_ecm") == 0,
	    "the driver starts and says hello as usb_ecm");
}

static void
no_violations(const char *when)
{
	check(hc.violations == 0, "%s: xhci's rules kept (%u: %s)", when,
	    hc.violations, hc.violation);
}

static void
pattern(uint8_t *b, size_t len, unsigned seed)
{
	size_t i;

	for (i = 0; i < len; i++)
		b[i] = (uint8_t)(i * 31 + seed * 7 + 1);
	if (len >= 14) {		/* a destination and a source */
		memset(b, 0xff, 6);
		b[12] = 0x08;
		b[13] = 0x06;
	}
}

/*===========================================================================*
 *    the checks                                                             *
 *===========================================================================*/
static void
test_tablet_setup(void)
{
	static const char *const want[] = {
		"80 08 0000 0 1",		/* GET_CONFIGURATION */
		"80 06 0200 0 1024",		/* configuration, index 0 */
		"80 06 0300 0 255",		/* string 0: the languages */
		"80 06 0303 1033 255",		/* string 3 in US English */
		"01 0b 0001 1 0",		/* SET_INTERFACE(1, alt 1) */
		"21 43 000c 0 0"		/* filter: directed, broadcast */
	};
	static const uint8_t mac[6] = { 0x06, 0x4d, 0x45, 0x43, 0x48, 0x4f };
	struct usb_urb *rx, *note;
	unsigned i;

	section("the Mi Pad 4's function: setting up");
	driver_fresh();
	dev_arrive(&tablet, 5);

	check(dv.nreq == 6, "%u control requests, the six Linux's cdc_ether "
	    "makes", dv.nreq);
	for (i = 0; i < 6 && i < dv.nreq; i++)
		check(strcmp(dv.req[i], want[i]) == 0, "request %u: %s "
		    "(want %s)", i, dv.req[i], want[i]);
	check(dv.unexpected == 0, "no request the device does not know");
	check(lw.announced == 1, "the interface is announced to lwip once");
	check(memcmp(lw.hwaddr, mac, 6) == 0, "with the station address of "
	    "iMACAddress, 06:4d:45:43:48:4f (%02x:%02x:...:%02x)",
	    lw.hwaddr[0], lw.hwaddr[1], lw.hwaddr[5]);
	check(dv.alt == 1 && dv.filter == 0x0c, "the data interface is open "
	    "(alt %u), filter 0x%02x", dv.alt, dv.filter);

	rx = wait_of(USB_TRANSFER_BLK);
	note = wait_of(USB_TRANSFER_INT);
	check(rx != NULL && rx->endpoint == 1 && rx->size == 2048 &&
	    (rx->transfer_flags & USB_URB_WAITS),
	    "a receive waits on endpoint 1 in: 2048 bytes, USB_URB_WAITS");
	check(note != NULL && note->endpoint == 2 && note->size == 16,
	    "a notification waits on endpoint 2 in: one packet, 16 bytes");
	check(lw.link_state == NDEV_LINK_UP, "lwip hears the link is up");
	check(lw.media == (IFM_ETHER | IFM_FDX | IFM_100_TX),
	    "at the speed SPEED_CHANGE gave: 0x%x", lw.media);
	no_violations("setting up");
	check(n_warn == 0, "the driver warned about nothing: %s", last_warn);
}

static void
test_frames_in(void)
{
	static uint8_t f98[98], f42[42], f1514[1514], f10[10], f1600[1600];
	static uint8_t f3000[3000], fa[200], fb[300];
	unsigned before, i, zero;

	section("frames in");
	pattern(f98, sizeof(f98), 1);
	pattern(f42, sizeof(f42), 2);
	pattern(f1514, sizeof(f1514), 3);
	pattern(fa, sizeof(fa), 4);
	pattern(fb, sizeof(fb), 5);

	lw_want(4);
	dev_send(f98, sizeof(f98));
	run(5);
	check(lw.rx_n == 1 && lw.rx[0].len == 98 &&
	    memcmp(lw.rx[0].b, f98, 98) == 0, "a frame of 98 bytes reaches "
	    "lwip as it was sent");

	dev_send(f42, sizeof(f42));
	run(5);
	check(lw.rx_n == 2 && lw.rx[1].len == 60, "an ARP request of 42 "
	    "bytes reaches lwip as 60 (%zu)", lw.rx[1].len);
	for (i = 42, zero = 1; i < 60; i++)
		if (lw.rx[1].b[i] != 0)
			zero = 0;
	check(memcmp(lw.rx[1].b, f42, 42) == 0 && zero,
	    "the frame, and then zeros - not the longer frame before it");

	dev_send(f1514, sizeof(f1514));
	run(5);
	check(lw.rx_n == 3 && lw.rx[2].len == 1514 &&
	    memcmp(lw.rx[2].b, f1514, 1514) == 0, "a full frame of 1514");

	/* Nothing, too short, too long, longer than the transfer. */
	before = lw.ierrors;
	dev_send(f98, 0);
	run(5);
	check(lw.ierrors == before && lw.rx_n == 3,
	    "a zero-length packet is not a frame, and not an error");
	dev_send(f10, sizeof(f10));
	run(5);
	check(lw.ierrors == before + 1 && lw.rx_n == 3,
	    "10 bytes is not a frame: one input error");
	dev_send(f1600, sizeof(f1600));
	run(5);
	check(lw.ierrors == before + 2 && lw.rx_n == 3,
	    "1600 bytes is not a frame either");
	dev_send(f3000, sizeof(f3000));
	run(5);
	check(lw.ierrors == before + 3, "3000 bytes overruns the transfer: "
	    "an error from the controller");
	dev_send(f98, sizeof(f98));
	run(30);
	check(lw.rx_n == 4 && lw.rx[3].len == 98,
	    "and the next frame arrives after the pause");

	/* lwip not asking: the frame is held, and nothing more is read. */
	dev_send(fa, sizeof(fa));
	dev_send(fb, sizeof(fb));
	run(10);
	check(lw.rx_n == 4, "lwip asks for nothing: nothing handed up");
	check(waits_of(USB_TRANSFER_BLK) == 0 && dv.in_n == 1,
	    "one frame is held, and the next waits in the device (%u)",
	    dv.in_n);
	lw_want(2);
	run(10);
	check(lw.rx_n == 6 && lw.rx[4].len == 200 && lw.rx[5].len == 300 &&
	    memcmp(lw.rx[4].b, fa, 200) == 0 &&
	    memcmp(lw.rx[5].b, fb, 300) == 0,
	    "lwip asks for two: both, in order");
	check(waits_of(USB_TRANSFER_BLK) == 1, "and a receive waits again");
	lw_want(8);
	no_violations("frames in");
}

static void
test_frames_out(void)
{
	static const size_t sizes[] = { 60, 511, 512, 1024, 1514 };
	static const size_t wire[] = { 60, 511, 513, 1025, 1514 };
	static uint8_t f[5][1514];
	unsigned i, ok = 1;

	section("frames out");
	dv.out_n = 0;
	for (i = 0; i < 5; i++) {
		pattern(f[i], sizes[i], 10 + i);
		lw_send(f[i], sizes[i]);
	}
	check(lw.sent < 5, "one frame at a time: %u taken at once", lw.sent);
	run(20);
	check(lw.sent == 5, "all five taken (%u)", lw.sent);
	check(dv.out_n == 5, "the device has five frames (%u)", dv.out_n);
	for (i = 0; i < 5 && i < dv.out_n; i++) {
		if (dv.out[i].len != wire[i] ||
		    memcmp(dv.out[i].b, f[i], sizes[i]) != 0 ||
		    (wire[i] > sizes[i] && dv.out[i].b[sizes[i]] != 0))
			ok = 0;
		check(dv.out[i].len == wire[i], "a frame of %zu goes as %zu "
		    "(%zu)", sizes[i], wire[i], dv.out[i].len);
	}
	check(ok, "with the bytes lwip gave, and a zero after a whole packet");
	no_violations("frames out");
}

static void
test_everything_at_once(void)
{
	static uint8_t f[20][700];
	unsigned i, inorder = 1;

	section("everything at once: frames both ways, the filter, news");
	dv.out_n = 0;
	lw.rx_n = 0;
	lw.recv_want = 0;
	for (i = 0; i < 20; i++)
		pattern(f[i], 100 + 30 * i, 40 + i);
	lw_want(10);
	for (i = 0; i < 10; i++) {
		dev_send(f[i], 100 + 30 * i);
		lw_send(f[10 + i], 100 + 30 * (10 + i));
		if (i == 3)
			ecm_table.ndr_set_mode(NDEV_MODE_UP | NDEV_MODE_BCAST |
			    NDEV_MODE_MCAST_LIST, NULL, 0);
		if (i == 5)
			note_speed(100000000, 100000000, 16);
		step();
	}
	run(40);
	check(lw.rx_n == 10, "ten frames in (%u)", lw.rx_n);
	for (i = 0; i < lw.rx_n; i++)
		if (lw.rx[i].len != (100 + 30 * i < 60 ? 60 : 100 + 30 * i) ||
		    memcmp(lw.rx[i].b, f[i], 100 + 30 * i) != 0)
			inorder = 0;
	check(inorder, "in order and whole");
	check(dv.out_n == 10 && lw.tx_n == 0, "ten frames out (%u)", dv.out_n);
	check(dv.filter == 0x0e, "multicast now passes: filter 0x%02x",
	    dv.filter);
	check(lw.media == (IFM_ETHER | IFM_FDX | IFM_100_TX),
	    "and the news of 100 Mbit/s arrived");
	no_violations("everything at once");
}

static void
test_filter(void)
{
	unsigned n;

	section("the packet filter");
	ecm_table.ndr_set_mode(NDEV_MODE_UP | NDEV_MODE_BCAST |
	    NDEV_MODE_PROMISC, NULL, 0);
	run(5);
	check(dv.filter == 0x0d, "promiscuous, no multicast asked: 0x%02x",
	    dv.filter);
	ecm_table.ndr_set_mode(NDEV_MODE_UP | NDEV_MODE_BCAST |
	    NDEV_MODE_MCAST_ALL, NULL, 0);
	run(5);
	check(dv.filter == 0x0e, "all multicast: 0x%02x", dv.filter);

	n = dv.filters;
	ecm_table.ndr_set_mode(NDEV_MODE_UP | NDEV_MODE_BCAST |
	    NDEV_MODE_MCAST_LIST, NULL, 0);
	run(5);
	check(dv.filters == n, "the same filter again is not sent again");

	dv.stall_filter = 1;
	n = dv.filter_stalls;
	ecm_table.ndr_set_mode(NDEV_MODE_UP | NDEV_MODE_BCAST, NULL, 0);
	run(50);
	check(dv.filter_stalls == n + 1, "a device that stalls the filter is "
	    "asked once (%u)", dv.filter_stalls - n);
	check(n_warn == 0, "and that is not a warning: %s", last_warn);
	dv.stall_filter = 0;
	no_violations("the filter");
}

static void
test_notifications(void)
{
	static uint8_t f[100];
	unsigned links, out, oerr;

	section("notifications: the link");
	pattern(f, sizeof(f), 77);
	links = lw.links;
	note_connection(0);
	run(5);
	check(lw.link_state == NDEV_LINK_DOWN && lw.links > links,
	    "NETWORK_CONNECTION 0: lwip hears the link is down");
	out = dv.out_n;
	oerr = lw.oerrors;
	lw_send(f, sizeof(f));
	run(5);
	check(dv.out_n == out && lw.oerrors == oerr + 1 && lw.tx_n == 0,
	    "a frame while it is down is dropped and counted");
	note_connection(1);
	run(5);
	check(lw.link_state == NDEV_LINK_UP, "NETWORK_CONNECTION 1: up again");
	lw_send(f, sizeof(f));
	run(5);
	check(dv.out_n == out + 1, "and frames go");
	check(waits_of(USB_TRANSFER_INT) == 1, "a notification waits again");
	no_violations("notifications");
}

static void
test_gone(void)
{
	static uint8_t f[100];
	unsigned oerr;

	section("the device goes, and comes back");
	pattern(f, sizeof(f), 88);
	dev_gone();
	check(lw.link_state == NDEV_LINK_DOWN, "gone: the link is down");
	oerr = lw.oerrors;
	lw_send(f, sizeof(f));
	check(lw.oerrors == oerr + 1 && lw.tx_n == 0,
	    "a frame for nobody is dropped and counted");

	dev_arrive(&tablet, 6);
	check(lw.announced == 1, "back: the same interface, not a second "
	    "(%u)", lw.announced);
	check(lw.link_state == NDEV_LINK_UP, "the link is up");
	dv.out_n = 0;
	lw_send(f, sizeof(f));
	run(5);
	check(dv.out_n == 1 && dv.out[0].len == 100, "frames go to it");
	lw_want(1);
	dev_send(f, sizeof(f));
	run(5);
	check(lw.rx_n > 0 && lw.rx[lw.rx_n - 1].len == 100, "and come from it");

	dev_gone();
	n_warn = 0;
	dev_arrive(&tablet_other, 7);
	check(n_warn == 1, "another station address: a warning (%s)",
	    last_warn);
	check(dv.nreq == 4 && dv.alt == 0, "and the device is not opened "
	    "(%u requests, alt %u)", dv.nreq, dv.alt);
	check(lw.link_state == NDEV_LINK_DOWN && lw.announced == 1,
	    "the interface stays, with its address, and down");
	check(waits_of(USB_TRANSFER_BLK) == 0, "nothing waits on the other");
	no_violations("going and coming back");
}

static void
test_adapter(void)
{
	static const uint8_t mac[6] = { 0x00, 0xe0, 0x4c, 0x36, 0xa1, 0xb2 };
	static uint8_t f[1024], g[512];
	struct usb_urb *note;

	section("an RTL8153: CDC Ethernet in the second configuration");
	driver_fresh();
	dev_arrive(&rtl, 3);

	check(dv.nreq >= 3 && strcmp(dv.req[1], "80 06 0200 0 1024") == 0 &&
	    strcmp(dv.req[2], "80 06 0201 0 1024") == 0,
	    "configuration index 0 is the vendor's, so index 1 is read too");
	check(dv.nreq >= 5 && strcmp(dv.req[4], "80 06 0306 1033 255") == 0,
	    "the station address is string 6 (%s)",
	    dv.nreq >= 5 ? dv.req[4] : "-");
	check(lw.announced == 1 && memcmp(lw.hwaddr, mac, 6) == 0,
	    "cdce0 is 00:e0:4c:36:a1:b2, from lower-case digits");
	note = wait_of(USB_TRANSFER_INT);
	check(note != NULL && note->endpoint == 3 && note->size == 8,
	    "notifications on endpoint 3, a packet of 8 at a time");
	check(lw.link_state == NDEV_LINK_UP, "the link is up, as Linux "
	    "assumes, until the device says otherwise");

	note_connection(0);
	run(5);
	check(lw.link_state == NDEV_LINK_DOWN, "no cable: down");
	note_connection(1);
	note_speed(1000000000, 1000000000, 8);
	run(10);
	check(lw.link_state == NDEV_LINK_UP &&
	    lw.media == (IFM_ETHER | IFM_FDX | IFM_1000_T),
	    "a cable at 1 Gbit/s: up, and SPEED_CHANGE in two packets "
	    "(media 0x%x)", lw.media);

	pattern(f, sizeof(f), 5);
	pattern(g, sizeof(g), 6);
	lw_want(1);
	dev_send(f, sizeof(f));
	lw_send(g, sizeof(g));
	run(10);
	check(lw.rx_n == 1 && lw.rx[0].len == 1024, "a frame in on endpoint "
	    "1");
	check(dv.out_n == 1 && dv.out[0].len == 513, "a frame out on "
	    "endpoint 2, 512 bytes as 513");
	no_violations("the adapter");
	check(n_warn == 0, "the driver warned about nothing: %s", last_warn);
}

/*
 * The user's dock: its RTL8152 three hubs down, which the driver does not
 * see - a device is a device - and which answers in its own ways: string
 * 3, capitals, sixteen-byte notifications whole, 100 Mbit/s.
 */
static void
test_dock_rtl8152(void)
{
	static const uint8_t mac[6] = { 0x00, 0xe0, 0x4c, 0x36, 0x70, 0x12 };
	static uint8_t f[1514], g[60];
	struct usb_urb *note;

	section("the dock's RTL8152, as it answered the board's Linux");
	driver_fresh();
	dev_arrive(&rtl8152, 9);

	check(dv.nreq >= 3 && strcmp(dv.req[2], "80 06 0201 0 1024") == 0,
	    "configuration index 1, CDC Ethernet, is read");
	check(dv.nreq >= 5 && strcmp(dv.req[4], "80 06 0303 1033 255") == 0,
	    "the station address is string 3 (%s)",
	    dv.nreq >= 5 ? dv.req[4] : "-");
	check(lw.announced == 1 && memcmp(lw.hwaddr, mac, 6) == 0,
	    "cdce0 is 00:e0:4c:36:70:12, its digits in capitals");
	check(dv.alt == 1, "the data interface is in alternate %u", dv.alt);
	note = wait_of(USB_TRANSFER_INT);
	check(note != NULL && note->endpoint == 3 && note->size == 16,
	    "notifications on endpoint 3, sixteen bytes at a time (%d)",
	    note != NULL ? (int)note->size : -1);

	note_connection(1);
	note_speed(100000000, 100000000, 16);
	run(10);
	check(lw.link_state == NDEV_LINK_UP &&
	    lw.media == (IFM_ETHER | IFM_FDX | IFM_100_TX),
	    "a cable at 100 Mbit/s, SPEED_CHANGE in one packet (media 0x%x)",
	    lw.media);

	pattern(f, sizeof(f), 7);
	pattern(g, sizeof(g), 8);
	lw_want(1);
	dev_send(f, sizeof(f));
	lw_send(g, sizeof(g));
	run(10);
	check(lw.rx_n == 1 && lw.rx[0].len == 1514, "a full frame in on "
	    "endpoint 1");
	check(dv.out_n == 1 && dv.out[0].len == 60, "a short one out on "
	    "endpoint 2");
	no_violations("the dock's RTL8152");
	check(n_warn == 0, "the driver warned about nothing: %s", last_warn);
}

/* Remove the descriptor at off from a copy of a configuration. */
static size_t
cfg_cut(uint8_t *out, const uint8_t *cfg, size_t len, size_t off)
{
	size_t n = cfg[off], total;

	memcpy(out, cfg, off);
	memcpy(out + off, cfg + off + n, len - off - n);
	total = len - n;
	out[2] = (uint8_t)total;
	out[3] = (uint8_t)(total >> 8);
	return total;
}

static void
test_parsing(void)
{
	struct ecm_info info;
	static uint8_t c[128];
	uint8_t s[64], mac[6];
	struct ecm_notes st = { 0 };
	struct ecm_note note;
	uint8_t n16[16] = { 0xa1, 0x2a, 0, 0, 0, 0, 8, 0 };
	size_t len;

	section("what the descriptors say, and what is refused");

	check(ecm_parse_config(tab_cfg, sizeof(tab_cfg), &info) == ECM_OK,
	    "the tablet's configuration is CDC Ethernet");
	check(info.ctrl_iface == 0 && info.data_iface == 1 &&
	    info.data_alt == 1 && info.bulk_in == 1 && info.bulk_out == 1 &&
	    info.bulk_in_size == 512 && info.note_ep == 2 &&
	    info.note_size == 16 && info.mac_string == 3 &&
	    info.max_segment == 1514, "interfaces 0 and 1, alt 1, endpoints 1 "
	    "and 2, string 3, 1514");

	check(ecm_parse_config(rtl_cfg0, sizeof(rtl_cfg0), &info) ==
	    ECM_NO_CONTROL, "the vendor's configuration is not");

	len = cfg_cut(c, tab_cfg, sizeof(tab_cfg), 23);	/* the union */
	check(ecm_parse_config(c, len, &info) == ECM_NO_UNION, "no union: %s",
	    ecm_parse_error(ecm_parse_config(c, len, &info)));
	len = cfg_cut(c, tab_cfg, sizeof(tab_cfg), 28);	/* Ethernet */
	check(ecm_parse_config(c, len, &info) == ECM_NO_ETHERNET,
	    "no Ethernet descriptor: %s",
	    ecm_parse_error(ecm_parse_config(c, len, &info)));
	len = cfg_cut(c, tab_cfg, sizeof(tab_cfg), 18);	/* the header */
	check(ecm_parse_config(c, len, &info) == ECM_NO_HEADER,
	    "no header: %s", ecm_parse_error(ecm_parse_config(c, len, &info)));
	len = cfg_cut(c, tab_cfg, sizeof(tab_cfg), 73);	/* bulk OUT */
	check(ecm_parse_config(c, len, &info) == ECM_NO_BULK,
	    "half a bulk pair: %s",
	    ecm_parse_error(ecm_parse_config(c, len, &info)));

	memcpy(c, tab_cfg, sizeof(tab_cfg));
	c[26] = 1;					/* master 1, */
	c[27] = 0;					/* subordinate 0 */
	check(ecm_parse_config(c, sizeof(tab_cfg), &info) == ECM_OK &&
	    info.data_iface == 1, "a union the wrong way round is turned "
	    "round");
	memcpy(c, tab_cfg, sizeof(tab_cfg));
	c[26] = 3;
	c[27] = 4;
	check(ecm_parse_config(c, sizeof(tab_cfg), &info) == ECM_NO_UNION,
	    "a union of two other interfaces is refused");
	memcpy(c, tab_cfg, sizeof(tab_cfg));
	c[44] = 2;					/* the note: bulk */
	check(ecm_parse_config(c, sizeof(tab_cfg), &info) == ECM_BAD_NOTE,
	    "one endpoint that is not interrupt in: %s",
	    ecm_parse_error(ecm_parse_config(c, sizeof(tab_cfg), &info)));
	memcpy(c, tab_cfg, sizeof(tab_cfg));
	c[53] = 0x02;					/* data alt 0: comm */
	check(ecm_parse_config(c, sizeof(tab_cfg), &info) == ECM_NO_DATA,
	    "a data interface of class 2: %s",
	    ecm_parse_error(ecm_parse_config(c, sizeof(tab_cfg), &info)));
	len = cfg_cut(c, tab_cfg, sizeof(tab_cfg), 41);	/* the note */
	c[13] = 0;
	check(ecm_parse_config(c, len, &info) == ECM_OK && info.note_ep == 0,
	    "no notifications at all is allowed");

	len = mac_string(s, sizeof(s), "064D4543484F");
	check(ecm_parse_mac(s, len, mac) && mac[0] == 0x06 && mac[5] == 0x4f,
	    "twelve upper-case digits");
	len = mac_string(s, sizeof(s), "00e04c36a1b2");
	check(ecm_parse_mac(s, len, mac) && mac[1] == 0xe0 && mac[5] == 0xb2,
	    "twelve lower-case digits");
	len = mac_string(s, sizeof(s), "064D4543484");
	check(!ecm_parse_mac(s, len, mac), "eleven are refused");
	len = mac_string(s, sizeof(s), "064D4543484F0");
	check(!ecm_parse_mac(s, len, mac), "thirteen are refused");
	len = mac_string(s, sizeof(s), "06:4D:45:43:");
	check(!ecm_parse_mac(s, len, mac), "colons are refused");
	len = mac_string(s, sizeof(s), "064D4543484F");
	s[5] = 0x30;
	check(!ecm_parse_mac(s, len, mac), "a character beyond ASCII is "
	    "refused");
	len = mac_string(s, sizeof(s), "064D4543484F");
	check(!ecm_parse_mac(s, len - 2, mac), "a string cut short is "
	    "refused");

	check(ecm_tx_length(511, 512) == 511 && ecm_tx_length(512, 512) ==
	    513 && ecm_tx_length(1024, 512) == 1025 &&
	    ecm_tx_length(1514, 512) == 1514 && ecm_tx_length(64, 64) == 65,
	    "a byte more only after a whole packet");

	put32(n16 + 8, 480000000);
	put32(n16 + 12, 12000000);
	check(ecm_note_feed(&st, n16, 16, &note) && note.what ==
	    ECM_NOTE_SPEED && note.down == 480000000 && note.up == 12000000,
	    "SPEED_CHANGE whole");
	check(!ecm_note_feed(&st, n16, 8, &note) && st.split,
	    "its header alone waits for the rates");
	check(ecm_note_feed(&st, n16 + 8, 8, &note) && note.what ==
	    ECM_NOTE_SPEED && note.down == 480000000 && !st.split,
	    "and the rates complete it");
	check(!ecm_note_feed(&st, n16, 7, &note), "seven bytes are nothing");
}

static void
test_refusals(void)
{
	static uint8_t c[128];
	size_t len;

	section("a device that will not open, and one that is not ECM");
	driver_fresh();
	memset(&dv, 0, sizeof(dv));
	dv.stall_alt = 1;
	dv.d = &tablet;
	dv.id = 9;
	dv.present = 1;
	ecm_usb.connect_device(9, 3);
	run(40);
	check(lw.announced == 0, "SET_INTERFACE stalled: no interface");
	check(n_warn == 1, "and it is said: %s", last_warn);
	check(waits_of(USB_TRANSFER_BLK) == 0 &&
	    waits_of(USB_TRANSFER_INT) == 0, "nothing waits");

	driver_fresh();
	len = cfg_cut(c, tab_cfg, sizeof(tab_cfg), 23);
	cur_cfg_override = c;
	cur_cfg_override_len = len;
	dev_arrive(&tablet, 10);
	check(dv.nreq == 2 && lw.announced == 0, "no union: refused after "
	    "the configuration (%u requests)", dv.nreq);
	check(n_warn == 1, "and it is said: %s", last_warn);
	cur_cfg_override = NULL;
	no_violations("refusals");
}

static void
test_stop(void)
{
	static uint8_t f[100];

	section("stopping");
	driver_fresh();
	dev_arrive(&tablet, 5);
	ecm_signal(SIGTERM);
	check(hc.deinits == 1 && lw.terminated == 1 && !running,
	    "SIGTERM: xhci is told, libnetdriver stops");

	driver_fresh();
	dev_arrive(&tablet, 5);
	pattern(f, sizeof(f), 1);
	lw_send(f, sizeof(f));
	check(tx_out, "a frame is out");
	ecm_signal(SIGTERM);
	check(hc.deinits == 0, "SIGTERM with a frame out: xhci is not asked "
	    "anything, since its answer would be taken for the completion");
	no_violations("stopping");
}

int
main(int argc, char **argv)
{
	if (argc > 1 && strcmp(argv[1], "-v") == 0)
		verbose = 1;

	test_tablet_setup();
	test_frames_in();
	test_frames_out();
	test_everything_at_once();
	test_filter();
	test_notifications();
	test_gone();
	test_adapter();
	test_dock_rtl8152();
	test_parsing();
	test_refusals();
	test_stop();

	printf("\n%u checks, %u failure(s)\n", checks, failures);
	return failures != 0 ? 1 : 0;
}
