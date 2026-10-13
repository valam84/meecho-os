/*
 * A USB mouse, through the boot protocol.
 *
 * The keyboard's twin (usb_kbd), and for the same reason: every USB mouse
 * has to be usable by a BIOS that parses no report descriptors, and so it
 * offers the boot protocol on an interface of class 3, subclass 1,
 * protocol 2 - a report of three bytes on an interrupt IN endpoint, the
 * buttons and the motion along X and Y since the last one.  This driver
 * asks for that protocol and reads those reports (report.c says what is
 * in them), and parses no descriptor.
 *
 * Where it sits.  Below it is the USB host controller service (label
 * "usbd" - xhci on this port), spoken to through libusb's URB protocol;
 * xhci chooses this driver for a device by its 3/1/2 interface and
 * announces it here - and to usb_kbd as well, if the same device also has
 * a keyboard interface, as a receiver for a keyboard and a mouse does.
 * Each driver then speaks to its own interface.  Above it is the input
 * server, spoken to through libinputdriver as a mouse: relative motion in
 * the general desktop page, buttons in the button page.  The input server
 * hands the events to the readers of /dev/mousemux and /dev/mouseN; unlike
 * key events, nothing takes them when nobody reads.
 *
 * Arguments (minix-service up ... -args): log=0..4, wheel=0|1 (1: take a
 * fourth report byte as the wheel), inject=REPORTS (move and click
 * without a mouse; see below).
 */

#include <minix/drivers.h>
#include <minix/ds.h>
#include <minix/input.h>
#include <minix/inputdriver.h>
#include <minix/log.h>
#include <minix/timers.h>
#include <minix/usb.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "report.h"

/* report.c speaks the input server's codes without its header. */
_Static_assert(MOUSE_PAGE_GD == INPUT_PAGE_GD &&
    MOUSE_PAGE_BUTTON == INPUT_PAGE_BUTTON && MOUSE_GD_X == INPUT_GD_X &&
    MOUSE_GD_Y == INPUT_GD_Y && MOUSE_GD_WHEEL == INPUT_GD_WHEEL &&
    MOUSE_BUTTON_1 == INPUT_BUTTON_1, "report.h and <minix/input.h> differ");

static struct log mouse_log = {
	.name = "usb_mouse",
	.log_level = LEVEL_INFO,
	.log_func = default_log
};

/*
 * The request blocks, two and no more, as in usb_kbd: one for the report
 * that waits on the interrupt endpoint, one for whatever control request
 * is going.  Each is a struct usb_urb followed by its data.
 */
#define CTL_DATA	256		/* a mouse's whole configuration */
#define REP_DATA	64		/* the largest interrupt packet taken */

static char ctl_mem[USB_URBSIZE(CTL_DATA, 0)] __attribute__((aligned(8)));
static char rep_mem[USB_URBSIZE(REP_DATA, 0)] __attribute__((aligned(8)));
#define CTL	((struct usb_urb *)ctl_mem)
#define REP	((struct usb_urb *)rep_mem)

/* Which way a transfer goes, in the value that travels (see usb_kbd.c). */
#define DIR_IN		1
#define DIR_OUT		0

/* HID class requests (HID 1.11, 7.2), on the mouse's interface. */
#define HID_SET_IDLE		0x0a
#define HID_SET_PROTOCOL	0x0b
#define HID_PROTOCOL_BOOT	0

/* Where a device is in being set up; one control request per step. */
enum stage {
	ST_NONE,		/* no mouse */
	ST_CONFIG,		/* reading the configuration descriptor */
	ST_PROTOCOL,		/* SET_PROTOCOL(boot) */
	ST_IDLE,		/* SET_IDLE(0) */
	ST_RUNNING		/* reports are being read */
};

static struct {
	enum stage stage;
	unsigned dev_id;
	unsigned iface;		/* the boot mouse interface */
	unsigned ep;		/* its interrupt IN endpoint */
	unsigned ep_size;	/* and how much one packet carries */

	int ctl_out;		/* the control block is with xhci */
	int rep_out;		/* the report block is with xhci */
	int rep_wanted;		/* a report should be asked for when it can */

	unsigned buttons;	/* what the last report held down */
	unsigned errors;	/* reports that failed in a row */
	unsigned long reports;	/* and that arrived, for the log */
} mouse;

static endpoint_t usbd_ep = NONE;
static int wheel = 1;

static minix_timer_t tmr_retry;		/* a failed report, again */

#define MAX_ERRORS	50		/* in a row; then the device is let go */

static void mouse_urb_completion(struct usb_urb *urb);
static void mouse_connect(unsigned dev_id, unsigned interfaces);
static void mouse_disconnect(unsigned dev_id);

static struct usb_driver mouse_usb = {
	.urb_completion		= mouse_urb_completion,
	.connect_device		= mouse_connect,
	.disconnect_device	= mouse_disconnect
};

/*===========================================================================*
 *    requests to xhci                                                       *
 *===========================================================================*/
/*
 * Hand a block to xhci.  The rule is usb_kbd's, and for its reason: the
 * completion of a control transfer is SENT, and a driver blocked in
 * sendrec at that moment would lose it or take it for the answer, so
 * nothing is sent while a control transfer is out - the report waits for
 * it (rep_wanted).
 */
static void
send_rep(void)
{
	if (mouse.stage != ST_RUNNING || mouse.rep_out)
		return;
	if (mouse.ctl_out) {
		mouse.rep_wanted = 1;
		return;
	}
	mouse.rep_wanted = 0;

	memset(rep_mem, 0, sizeof(rep_mem));
	REP->urb_size = sizeof(rep_mem);
	REP->dev_id = (int)mouse.dev_id;
	REP->type = USB_TRANSFER_INT;
	REP->endpoint = (int)mouse.ep;
	REP->direction = DIR_IN;
	REP->size = mouse.ep_size;

	mouse.rep_out = 1;
	if (usb_send_urb(REP) != OK) {
		mouse.rep_out = 0;
		log_warn(&mouse_log, "the report could not be asked for\n");
	}
}

static void
send_ctl(uint8_t type, uint8_t request, uint16_t value, uint16_t index,
	uint16_t length)
{
	struct usb_ctrlrequest *req;

	memset(ctl_mem, 0, sizeof(ctl_mem));
	CTL->urb_size = sizeof(ctl_mem);
	CTL->dev_id = (int)mouse.dev_id;
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

	mouse.ctl_out = 1;
	if (usb_send_urb(CTL) != OK) {
		mouse.ctl_out = 0;
		log_warn(&mouse_log, "control request 0x%02x could not be "
		    "sent\n", request);
	}
}

/*===========================================================================*
 *    reports                                                                *
 *===========================================================================*/
static void
report_event(unsigned page, unsigned code, int value, int rel, void *arg)
{
	log_debug(&mouse_log, "%s 0x%02x %d\n",
	    page == INPUT_PAGE_BUTTON ? "button" : "axis", code, value);
	inputdriver_send_event(TRUE /*mouse*/, page, code, value,
	    rel ? INPUT_FLAG_REL : 0);
}

static void
report(const uint8_t *rep, size_t len)
{
	char hex[3 * 8 + 1];
	size_t i;

	/* log=4: the report as it came, for a mouse that sends what the
	 * boot protocol does not say (a button outside the first three bits,
	 * a report ID in front). */
	if (mouse_log.log_level >= LEVEL_TRACE) {
		for (i = 0; i < len && i < 8; i++)
			snprintf(hex + 3 * i, sizeof(hex) - 3 * i, " %02x",
			    rep[i]);
		hex[3 * i] = '\0';
		log_trace(&mouse_log, "report %zu byte(s):%s\n", len, hex);
	}
	mouse.buttons = mouse_report(mouse.buttons, rep, len, wheel,
	    report_event, NULL);
}

/*
 * Every button that is down comes up: the mouse has gone, or this driver
 * is going.  A reader that saw a press must see its release, or a drag
 * in the compositor never ends.
 */
static void
release_all(void)
{
	static const uint8_t none[MOUSE_REPORT_MIN];

	report(none, sizeof(none));
}

/*===========================================================================*
 *    the device                                                             *
 *===========================================================================*/
static void
forget(const char *why)
{
	if (mouse.stage == ST_NONE)
		return;
	log_info(&mouse_log, "mouse on device %u: %s (%lu report(s))\n",
	    mouse.dev_id, why, mouse.reports);
	release_all();
	cancel_timer(&tmr_retry);
	memset(&mouse, 0, sizeof(mouse));
	mouse.stage = ST_NONE;
}

/*
 * The configuration descriptor: which interface is the boot mouse and
 * which endpoint its reports come from.  The same walk as usb_kbd's, for
 * protocol 2 instead of 1 - on a combined receiver the keyboard's
 * interface comes first, and must be passed over, not taken.
 */
static int
find_mouse(const uint8_t *cfg, size_t len)
{
	size_t off;
	int in_mouse = 0, found = 0;

	for (off = 0; off + 2 <= len && cfg[off] >= 2; off += cfg[off]) {
		if (cfg[off + 1] == 4 && off + 9 <= len) {	/* interface */
			in_mouse = !found && cfg[off + 3] == 0 &&
			    cfg[off + 5] == 3 && cfg[off + 6] == 1 &&
			    cfg[off + 7] == 2;
			if (in_mouse)
				mouse.iface = cfg[off + 2];
		}
		if (in_mouse && cfg[off + 1] == 5 && off + 7 <= len &&
		    (cfg[off + 2] & 0x80) && (cfg[off + 3] & 0x3) == 3) {
			mouse.ep = cfg[off + 2] & 0x0f;
			mouse.ep_size = (cfg[off + 4] | (cfg[off + 5] << 8)) &
			    0x7ff;
			if (mouse.ep_size > REP_DATA)
				mouse.ep_size = REP_DATA;
			found = 1;
			in_mouse = 0;
		}
	}
	return found;
}

static void
mouse_connect(unsigned dev_id, unsigned interfaces)
{
	if (mouse.stage != ST_NONE) {
		log_warn(&mouse_log, "device %u is a second mouse; only one "
		    "is driven, and that is device %u\n", dev_id,
		    mouse.dev_id);
		return;
	}

	memset(&mouse, 0, sizeof(mouse));
	mouse.dev_id = dev_id;
	mouse.stage = ST_CONFIG;

	log_info(&mouse_log, "device %u is announced (interfaces 0x%x)\n",
	    dev_id, interfaces);

	send_ctl(0x80, 6 /* GET_DESCRIPTOR */, 2 << 8 /* configuration */, 0,
	    CTL_DATA);
}

static void
mouse_disconnect(unsigned dev_id)
{
	if (mouse.stage != ST_NONE && dev_id == mouse.dev_id)
		forget("withdrawn");
}

/* A control request came back: the next step of setting up. */
static void
ctl_done(struct usb_urb *urb)
{
	mouse.ctl_out = 0;

	if (urb->status == ENODEV) {
		forget("gone");
		return;
	}

	switch (mouse.stage) {
	case ST_CONFIG:
		if (urb->status != OK || !find_mouse((uint8_t *)urb->buffer,
		    urb->actual_length)) {
			log_warn(&mouse_log, "device %u: no boot mouse in its "
			    "configuration (status %d, %u bytes)\n",
			    mouse.dev_id, urb->status,
			    (unsigned)urb->actual_length);
			forget("not a mouse after all");
			return;
		}
		log_info(&mouse_log, "device %u: boot mouse on interface %u, "
		    "reports on endpoint %u, %u bytes\n", mouse.dev_id,
		    mouse.iface, mouse.ep, mouse.ep_size);
		mouse.stage = ST_PROTOCOL;
		send_ctl(0x21, HID_SET_PROTOCOL, HID_PROTOCOL_BOOT, mouse.iface,
		    0);
		return;

	case ST_PROTOCOL:
		/*
		 * A mouse comes up in report protocol, which is the one
		 * thing worth knowing about this request: refused, the
		 * reports are the device's own layout, and on many mice that
		 * begins with the same three bytes anyway.  So it is read
		 * regardless, and the log says why it may look odd.
		 */
		if (urb->status != OK)
			log_warn(&mouse_log, "device %u refused the boot "
			    "protocol (%d); reading it anyway\n",
			    mouse.dev_id, urb->status);
		mouse.stage = ST_IDLE;
		send_ctl(0x21, HID_SET_IDLE, 0 /* only on change */,
		    mouse.iface, 0);
		return;

	case ST_IDLE:
		if (urb->status != OK)
			log_info(&mouse_log, "device %u did not take SET_IDLE "
			    "(%d), as many do\n", mouse.dev_id, urb->status);
		mouse.stage = ST_RUNNING;
		log_info(&mouse_log, "mouse on device %u is ready\n",
		    mouse.dev_id);
		send_rep();
		return;

	default:
		if (mouse.rep_wanted)
			send_rep();
		return;
	}
}

static void
retry_fire(int arg)
{
	send_rep();
}

/* A report came back - the mouse moved or clicked - or the transfer failed. */
static void
rep_done(struct usb_urb *urb)
{
	mouse.rep_out = 0;

	if (urb->status == ENODEV) {
		forget("gone");
		return;
	}

	if (urb->status != OK) {
		/* What usb_kbd does, for the reasons written there. */
		if (++mouse.errors >= MAX_ERRORS) {
			forget("too many failed reports");
			return;
		}
		log_debug(&mouse_log, "a report failed (%d), %u in a row\n",
		    urb->status, mouse.errors);
		set_timer(&tmr_retry, (clock_t)sys_hz() / 10, retry_fire, 0);
		return;
	}

	mouse.errors = 0;
	mouse.reports++;
	if (urb->actual_length != 0)
		report((uint8_t *)urb->buffer, urb->actual_length);

	send_rep();
}

static void
mouse_urb_completion(struct usb_urb *urb)
{
	if (urb == CTL)
		ctl_done(urb);
	else if (urb == REP)
		rep_done(urb);
	else
		log_warn(&mouse_log, "a completion for a block that is not "
		    "ours\n");
}

/*===========================================================================*
 *    the input driver                                                       *
 *===========================================================================*/
static void
mouse_alarm(clock_t stamp)
{
	expire_timers(stamp);
}

/* Everything that is not the input protocol: the USB protocol, from xhci. */
static void
mouse_other(message *m, int ipc_status)
{
	if (is_ipc_notify(ipc_status))
		return;

	if (usbd_ep != NONE && m->m_source == usbd_ep &&
	    (m->m_type == USB_COMPLETE_URB || m->m_type == USB_ANNOUCE_DEV ||
	    m->m_type == USB_WITHDRAW_DEV)) {
		usb_handle_msg(&mouse_usb, m);
		return;
	}

	log_warn(&mouse_log, "unexpected message 0x%x from %d\n", m->m_type,
	    m->m_source);
}

static struct inputdriver mouse_tab = {
	.idr_alarm	= mouse_alarm,
	.idr_other	= mouse_other
};

/*===========================================================================*
 *    moving without a mouse                                                 *
 *===========================================================================*/
/*
 * inject=REPORTS: boot reports, put through report() one per tick as if
 * the mouse had sent them, starting a second after the driver comes up.
 *
 * It proves the half of the path that has nothing to do with USB -
 * report.c, libinputdriver, the input server and its mouse devices -
 * where there is no USB mouse: on QEMU, and on the board with nobody at
 * it.  REPORTS is a comma-separated list of B:X:Y or B:X:Y:W, each field
 * a decimal number - B the button bits, X, Y and W signed bytes - so
 * "0:10:-5,1:0:0,0:0:0" moves right and up and then clicks the left
 * button.  injectwait=N waits N seconds instead of one.
 *
 * An instance given inject= does not speak to the host controller at all,
 * even where there is one: it is a stand-in, under a label of its own,
 * and taking a client slot in xhci for it would buy nothing.
 */
#define INJECT_MAX	256
#define INJECT_REPORTS	64

static uint8_t inject_rep[INJECT_REPORTS][4];
static unsigned inject_n, inject_pos;
static minix_timer_t tmr_inject;

static void
inject_step(int arg)
{
	if (inject_pos >= inject_n) {
		log_info(&mouse_log, "inject: sent %u report(s)\n", inject_n);
		return;
	}
	report(inject_rep[inject_pos], 4);
	inject_pos++;
	set_timer(&tmr_inject, 1, inject_step, 0);
}

/* Answers the number of reports, or -1 if the list does not parse. */
static int
inject_parse(const char *s)
{
	unsigned n = 0, f;
	long v;
	char *end;

	while (*s != '\0') {
		if (n >= INJECT_REPORTS)
			return -1;
		memset(inject_rep[n], 0, sizeof(inject_rep[n]));
		for (f = 0; f < 4; f++) {
			v = strtol(s, &end, 10);
			if (end == s || (f == 0 ? (v < 0 || v > 255) :
			    (v < -128 || v > 127)))
				return -1;
			inject_rep[n][f] = (uint8_t)v;
			s = end;
			if (*s != ':')
				break;
			s++;
		}
		if (f < 2)			/* B:X:Y at least */
			return -1;
		n++;
		if (*s == ',')
			s++;
		else if (*s != '\0')
			return -1;
	}
	return (int)n;
}

/*===========================================================================*
 *    start and stop                                                         *
 *===========================================================================*/
static int
mouse_init(int type, sef_init_info_t *info)
{
	static char name[] = "usb_mouse";	/* libusb takes no const */
	char arg[INJECT_MAX];
	long v, inject_wait = 1;
	int r;

	if (env_parse("log", "d", 0, &v, LEVEL_NONE, LEVEL_TRACE) == EP_SET)
		mouse_log.log_level = (int)v;
	if (env_parse("wheel", "d", 0, &v, 0, 1) == EP_SET)
		wheel = (int)v;
	if (env_get_param("inject", arg, sizeof(arg)) == OK) {
		if ((r = inject_parse(arg)) <= 0) {
			log_warn(&mouse_log, "inject=: B:X:Y[:W],... expected\n");
			return EINVAL;
		}
		inject_n = (unsigned)r;
	}
	(void)env_parse("injectwait", "d", 0, &inject_wait, 1, 600);

	init_timer(&tmr_retry);
	init_timer(&tmr_inject);
	memset(&mouse, 0, sizeof(mouse));
	mouse.stage = ST_NONE;

	/*
	 * The host controller, unless this instance only injects; and
	 * refused politely when it is not there, because libusb's
	 * usb_init() panics and RS would restart the panic for ever.
	 */
	if (inject_n == 0) {
		if ((r = ds_retrieve_label_endpt("usbd", &usbd_ep)) != OK) {
			log_warn(&mouse_log, "no USB host controller is "
			    "running (no \"usbd\": %d)\n", r);
			return r;
		}
		usb_init(name);
	}

	inputdriver_announce(INPUT_DEV_MOUSE);

	if (inject_n != 0) {
		log_info(&mouse_log, "injecting %u report(s) and nothing "
		    "else\n", inject_n);
		set_timer(&tmr_inject, (clock_t)inject_wait * sys_hz(),
		    inject_step, 0);
	} else
		log_info(&mouse_log, "waiting for a mouse\n");
	return OK;
}

/*
 * On the way out: let go of every button, and tell xhci this driver is
 * gone, so that the report still waiting on the ring is answered to nobody.
 */
static void
mouse_signal(int signo)
{
	message m;

	if (signo != SIGTERM)
		return;

	release_all();

	if (usbd_ep != NONE) {
		memset(&m, 0, sizeof(m));
		m.m_type = USB_RQ_DEINIT;
		(void)ipc_sendrec(usbd_ep, &m);
	}

	inputdriver_terminate();
}

int
main(int argc, char *argv[])
{
	/* Without this env_parse() sees none of the -args. */
	env_setargs(argc, argv);

	sef_setcb_init_fresh(mouse_init);
	sef_setcb_signal_handler(mouse_signal);
	sef_startup();

	inputdriver_task(&mouse_tab);

	return 0;
}
