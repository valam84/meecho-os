/*
 * A USB keyboard, through the boot protocol.
 *
 * Every USB keyboard has to be usable by a BIOS that knows nothing of HID
 * report descriptors, and so every one of them offers the boot protocol on
 * an interface of class 3, subclass 1, protocol 1: an eight-byte report on
 * an interrupt IN endpoint - a byte of modifier bits, a reserved byte, and
 * up to six key codes held down.  This driver asks for that protocol and
 * reads those reports.  It parses no report descriptor and drives nothing
 * else on the device, which is the whole of the trade: one table of
 * meaning, fixed by the HID specification, against a parser for arbitrary
 * descriptors that nothing here needs yet.
 *
 * Where it sits.  Below it is the USB host controller service (label
 * "usbd" - xhci on this port), spoken to through libusb's URB protocol;
 * xhci chooses this driver for a device by its 3/1/1 interface and
 * announces it here.  Above it is the input server, spoken to through
 * libinputdriver exactly as the PC keyboard driver (pckbd) does: a press
 * or a release of a key, in the keyboard usage page.  The input server
 * hands the events to /dev/kbdmux readers, or to tty when nobody reads
 * them; tty turns them into characters with its keymap and puts them into
 * the console line.  So this file knows nothing of characters, layouts or
 * terminals - the codes in a boot report ARE the input server's codes
 * (<minix/input.h> takes them from the same page of the same
 * specification), and the job here is only to turn "the keys held now"
 * into "this key went down, that one came up".
 *
 * Key repeat is done here, because it is where pckbd's comes from too: a
 * PC keyboard repeats in hardware and sends the press again, and tty and
 * the input server expect exactly that.  A USB keyboard reports a held key
 * once and says nothing until it changes, so the press is sent again from
 * a timer - after "delay" milliseconds, then every 1000/"rate".
 *
 * Arguments (minix-service up ... -args): log=0..4, delay=ms (500),
 * rate=per second (30), inject=TEXT (type TEXT; see "typing without a
 * keyboard" below).
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

static struct log kbd_log = {
	.name = "usb_kbd",
	.log_level = LEVEL_INFO,
	.log_func = default_log
};

/*
 * The request blocks, two and no more: one for the report that waits on
 * the interrupt endpoint, one for whatever control request is going.  Each
 * is a struct usb_urb followed by its data, which is how libusb expects to
 * find them - it grants the block from the second field to the end.
 */
#define CTL_DATA	256		/* a keyboard's whole configuration */
#define REP_DATA	64		/* the largest interrupt packet taken */

static char ctl_mem[USB_URBSIZE(CTL_DATA, 0)] __attribute__((aligned(8)));
static char rep_mem[USB_URBSIZE(REP_DATA, 0)] __attribute__((aligned(8)));
#define CTL	((struct usb_urb *)ctl_mem)
#define REP	((struct usb_urb *)rep_mem)

/*
 * Which way a transfer goes, in the value that travels.  <minix/usb.h>
 * says USB_IN is 0; xhci reads 1 as "in", because that is what the DDEKit
 * clients - every other client of this protocol - send (<ddekit/usb.h>).
 * See urb_dir_in() in xhci_urb.c, where the confusion is written down.
 */
#define DIR_IN		1
#define DIR_OUT		0

/* HID class requests (HID 1.11, 7.2), on the keyboard's interface. */
#define HID_SET_REPORT		0x09
#define HID_SET_IDLE		0x0a
#define HID_SET_PROTOCOL	0x0b
#define HID_REPORT_OUTPUT	2
#define HID_PROTOCOL_BOOT	0

/* The lock keys, which toggle and so must not repeat. */
#define KEY_CAPS_LOCK		0x39
#define KEY_SCROLL_LOCK		0x47
#define KEY_NUM_LOCK		0x53

/*
 * Where a device is in being set up.  Each step is one control request,
 * and the next one is sent from the completion of the last: one request
 * outstanding at a time, which is also what keeps this driver from ever
 * being inside a sendrec to xhci while xhci has an answer for it (see
 * send()).
 */
enum stage {
	ST_NONE,		/* no keyboard */
	ST_CONFIG,		/* reading the configuration descriptor */
	ST_PROTOCOL,		/* SET_PROTOCOL(boot) */
	ST_IDLE,		/* SET_IDLE(0) */
	ST_RUNNING		/* reports are being read */
};

static struct {
	enum stage stage;
	unsigned dev_id;
	unsigned iface;		/* the boot keyboard interface */
	unsigned ep;		/* its interrupt IN endpoint */
	unsigned ep_size;	/* and how much one packet carries */

	int ctl_out;		/* the control block is with xhci */
	int rep_out;		/* the report block is with xhci */
	int rep_wanted;		/* a report should be asked for when it can */

	uint8_t held[KBD_REPORT_LEN];	/* the last report: what is down */
	unsigned errors;	/* reports that failed in a row */
	unsigned long reports;	/* and that arrived, for the log */

	uint8_t leds;		/* what the LEDs should show (HID bits) */
	int leds_sent;		/* -1: never; else what was last sent */
} kbd;

static endpoint_t usbd_ep;

static minix_timer_t tmr_repeat;	/* the held key, again */
static minix_timer_t tmr_retry;		/* a failed report, again */
static unsigned rep_code;		/* the key that repeats; 0: none */
static clock_t rep_delay, rep_period;

#define MAX_ERRORS	50		/* in a row; then the device is let go */

static void kbd_urb_completion(struct usb_urb *urb);
static void kbd_connect(unsigned dev_id, unsigned interfaces);
static void kbd_disconnect(unsigned dev_id);

static struct usb_driver kbd_usb = {
	.urb_completion		= kbd_urb_completion,
	.connect_device		= kbd_connect,
	.disconnect_device	= kbd_disconnect
};

/*===========================================================================*
 *    requests to xhci                                                       *
 *===========================================================================*/
/*
 * Hand a block to xhci.
 *
 * libusb sends it with sendrec and takes whatever comes back as the
 * answer.  xhci posts the completions of interrupt transfers, which can
 * never be taken for an answer; but the completion of a control transfer
 * it SENDS, and if this driver were blocked in sendrec at that moment the
 * completion would either be lost (the kernel refuses a send to a process
 * that is itself sending to the sender) or taken for the answer, and
 * libusb would panic.  So nothing is sent while a control transfer is
 * out: the report waits for it (rep_wanted), and the LEDs wait for it.
 */
static void
send_rep(void)
{
	if (kbd.stage != ST_RUNNING || kbd.rep_out)
		return;
	if (kbd.ctl_out) {
		kbd.rep_wanted = 1;
		return;
	}
	kbd.rep_wanted = 0;

	memset(rep_mem, 0, sizeof(rep_mem));
	REP->urb_size = sizeof(rep_mem);
	REP->dev_id = (int)kbd.dev_id;
	REP->type = USB_TRANSFER_INT;
	REP->endpoint = (int)kbd.ep;
	REP->direction = DIR_IN;
	REP->size = kbd.ep_size;

	kbd.rep_out = 1;
	if (usb_send_urb(REP) != OK) {
		kbd.rep_out = 0;
		log_warn(&kbd_log, "the report could not be asked for\n");
	}
}

static void
send_ctl(uint8_t type, uint8_t request, uint16_t value, uint16_t index,
	const void *out, uint16_t length)
{
	struct usb_ctrlrequest *req;

	memset(ctl_mem, 0, sizeof(ctl_mem));
	CTL->urb_size = sizeof(ctl_mem);
	CTL->dev_id = (int)kbd.dev_id;
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

	if (out != NULL && length != 0)
		memcpy(CTL->buffer, out, length);

	kbd.ctl_out = 1;
	if (usb_send_urb(CTL) != OK) {
		kbd.ctl_out = 0;
		log_warn(&kbd_log, "control request 0x%02x could not be "
		    "sent\n", request);
	}
}

/* The LEDs, if they are not what they should be and the way is clear. */
static void
send_leds(void)
{
	if (kbd.stage != ST_RUNNING || kbd.ctl_out)
		return;
	if (kbd.leds_sent == (int)kbd.leds)
		return;

	kbd.leds_sent = kbd.leds;
	send_ctl(0x21, HID_SET_REPORT, HID_REPORT_OUTPUT << 8, kbd.iface,
	    &kbd.leds, 1);
}

/*===========================================================================*
 *    keys                                                                   *
 *===========================================================================*/
static void
key_event(unsigned code, int value)
{
	log_debug(&kbd_log, "key 0x%02x %s\n", code,
	    value == INPUT_PRESS ? "down" : "up");
	inputdriver_send_event(FALSE /*mouse*/, INPUT_PAGE_KEY, code, value, 0);
}

static void
repeat_fire(int arg)
{
	if (rep_code == 0)
		return;
	key_event(rep_code, INPUT_PRESS);
	set_timer(&tmr_repeat, rep_period, repeat_fire, 0);
}

static void
repeat_start(unsigned code)
{
	if (code >= KBD_KEY_MOD_FIRST || code == KEY_CAPS_LOCK ||
	    code == KEY_SCROLL_LOCK || code == KEY_NUM_LOCK) {
		return;
	}
	rep_code = code;
	set_timer(&tmr_repeat, rep_delay, repeat_fire, 0);
}

static void
repeat_stop(void)
{
	rep_code = 0;
	cancel_timer(&tmr_repeat);
}

/*
 * One change between two reports.  The key that repeats is the one
 * pressed last - the order of kbd_report_diff() makes that the last key
 * of the report - and it stops when it comes up, not when another does.
 */
static void
report_event(unsigned code, int press, void *arg)
{
	if (press) {
		key_event(code, INPUT_PRESS);
		repeat_start(code);
	} else {
		if (code == rep_code)
			repeat_stop();
		key_event(code, INPUT_RELEASE);
	}
}

/* A report: what is held down now, and so what changed (report.c). */
static void
report(const uint8_t *rep, size_t len)
{
	uint8_t now[KBD_REPORT_LEN];

	memset(now, 0, sizeof(now));
	memcpy(now, rep, len < sizeof(now) ? len : sizeof(now));

	if (kbd_report_diff(kbd.held, now, report_event, NULL))
		memcpy(kbd.held, now, sizeof(now));
}

/*
 * Everything that is down comes up: the keyboard has gone, or this driver
 * is going.  Without it tty would keep a shift or a control pressed for
 * ever, and every key typed afterwards on the serial line's neighbour
 * would come out shifted.
 */
static void
release_all(void)
{
	uint8_t none[KBD_REPORT_LEN];

	repeat_stop();
	memset(none, 0, sizeof(none));
	report(none, sizeof(none));
}

/*===========================================================================*
 *    the device                                                             *
 *===========================================================================*/
static void
forget(const char *why)
{
	if (kbd.stage == ST_NONE)
		return;
	log_info(&kbd_log, "keyboard on device %u: %s (%lu report(s))\n",
	    kbd.dev_id, why, kbd.reports);
	release_all();
	cancel_timer(&tmr_retry);
	memset(&kbd, 0, sizeof(kbd));
	kbd.stage = ST_NONE;
	kbd.leds_sent = -1;
}

/*
 * The configuration descriptor: which interface is the boot keyboard and
 * which endpoint its reports come from.  xhci has already chosen this
 * driver by that interface and set the configuration; this only finds the
 * numbers again, from the device's own answer.
 */
static int
find_keyboard(const uint8_t *cfg, size_t len)
{
	size_t off;
	int in_kbd = 0, found = 0;

	for (off = 0; off + 2 <= len && cfg[off] >= 2; off += cfg[off]) {
		if (cfg[off + 1] == 4 && off + 9 <= len) {	/* interface */
			in_kbd = !found && cfg[off + 3] == 0 &&
			    cfg[off + 5] == 3 && cfg[off + 6] == 1 &&
			    cfg[off + 7] == 1;
			if (in_kbd)
				kbd.iface = cfg[off + 2];
		}
		if (in_kbd && cfg[off + 1] == 5 && off + 7 <= len &&
		    (cfg[off + 2] & 0x80) && (cfg[off + 3] & 0x3) == 3) {
			kbd.ep = cfg[off + 2] & 0x0f;
			kbd.ep_size = (cfg[off + 4] | (cfg[off + 5] << 8)) &
			    0x7ff;
			if (kbd.ep_size > REP_DATA)
				kbd.ep_size = REP_DATA;
			found = 1;
			in_kbd = 0;
		}
	}
	return found;
}

static void
kbd_connect(unsigned dev_id, unsigned interfaces)
{
	if (kbd.stage != ST_NONE) {
		log_warn(&kbd_log, "device %u is a second keyboard; only one "
		    "is driven, and that is device %u\n", dev_id, kbd.dev_id);
		return;
	}

	memset(&kbd, 0, sizeof(kbd));
	kbd.dev_id = dev_id;
	kbd.leds_sent = -1;
	kbd.stage = ST_CONFIG;

	log_info(&kbd_log, "device %u is announced (interfaces 0x%x)\n",
	    dev_id, interfaces);

	send_ctl(0x80, 6 /* GET_DESCRIPTOR */, 2 << 8 /* configuration */, 0,
	    NULL, CTL_DATA);
}

static void
kbd_disconnect(unsigned dev_id)
{
	if (kbd.stage != ST_NONE && dev_id == kbd.dev_id)
		forget("withdrawn");
}

/* A control request came back: the next step of setting up, or the LEDs. */
static void
ctl_done(struct usb_urb *urb)
{
	kbd.ctl_out = 0;

	if (urb->status == ENODEV) {
		forget("gone");
		return;
	}

	switch (kbd.stage) {
	case ST_CONFIG:
		if (urb->status != OK || !find_keyboard((uint8_t *)urb->buffer,
		    urb->actual_length)) {
			log_warn(&kbd_log, "device %u: no boot keyboard in its "
			    "configuration (status %d, %u bytes)\n", kbd.dev_id,
			    urb->status, (unsigned)urb->actual_length);
			forget("not a keyboard after all");
			return;
		}
		log_info(&kbd_log, "device %u: boot keyboard on interface %u, "
		    "reports on endpoint %u, %u bytes\n", kbd.dev_id,
		    kbd.iface, kbd.ep, kbd.ep_size);
		kbd.stage = ST_PROTOCOL;
		send_ctl(0x21, HID_SET_PROTOCOL, HID_PROTOCOL_BOOT, kbd.iface,
		    NULL, 0);
		return;

	case ST_PROTOCOL:
		/*
		 * A boot-subclass keyboard has to take this.  One that
		 * refuses is still read, on the chance that it was in boot
		 * protocol already - which is where a device comes up.
		 */
		if (urb->status != OK)
			log_warn(&kbd_log, "device %u refused the boot "
			    "protocol (%d); reading it anyway\n", kbd.dev_id,
			    urb->status);
		kbd.stage = ST_IDLE;
		send_ctl(0x21, HID_SET_IDLE, 0 /* only on change */,
		    kbd.iface, NULL, 0);
		return;

	case ST_IDLE:
		/* Many keyboards stall SET_IDLE; nothing depends on it. */
		if (urb->status != OK)
			log_info(&kbd_log, "device %u did not take SET_IDLE "
			    "(%d), as many do\n", kbd.dev_id, urb->status);
		kbd.stage = ST_RUNNING;
		log_info(&kbd_log, "keyboard on device %u is ready\n",
		    kbd.dev_id);
		send_rep();
		send_leds();
		return;

	case ST_RUNNING:
		if (urb->status != OK)
			log_info(&kbd_log, "device %u: the LEDs were not set "
			    "(%d)\n", kbd.dev_id, urb->status);
		/* What waited for this request goes now. */
		if (kbd.rep_wanted)
			send_rep();
		send_leds();
		return;

	default:
		return;
	}
}

static void
retry_fire(int arg)
{
	send_rep();
}

/* A report came back - a key changed - or the transfer failed. */
static void
rep_done(struct usb_urb *urb)
{
	kbd.rep_out = 0;

	if (urb->status == ENODEV) {
		forget("gone");
		return;
	}

	if (urb->status != OK) {
		/*
		 * A failed report is what an unplugged keyboard looks like
		 * before the hub says it is gone, and what a lost split
		 * transaction looks like on a good day.  So: again, a
		 * little later, and give up only on a long run of them.
		 */
		if (++kbd.errors >= MAX_ERRORS) {
			forget("too many failed reports");
			return;
		}
		log_debug(&kbd_log, "a report failed (%d), %u in a row\n",
		    urb->status, kbd.errors);
		set_timer(&tmr_retry, (clock_t)sys_hz() / 10, retry_fire, 0);
		return;
	}

	kbd.errors = 0;
	kbd.reports++;
	if (urb->actual_length != 0)
		report((uint8_t *)urb->buffer, urb->actual_length);

	send_rep();
}

static void
kbd_urb_completion(struct usb_urb *urb)
{
	if (urb == CTL)
		ctl_done(urb);
	else if (urb == REP)
		rep_done(urb);
	else
		log_warn(&kbd_log, "a completion for a block that is not "
		    "ours\n");
}

/*===========================================================================*
 *    the input driver                                                       *
 *===========================================================================*/
/*
 * The input server's LED mask, bit (1 << INPUT_LED_x), into the boot
 * protocol's output report: Num Lock bit 0, Caps Lock 1, Scroll Lock 2.
 */
static void
kbd_leds(unsigned int mask)
{
	uint8_t hid = 0;

	if (mask & (1 << INPUT_LED_NUMLOCK))
		hid |= 0x01;
	if (mask & (1 << INPUT_LED_CAPSLOCK))
		hid |= 0x02;
	if (mask & (1 << INPUT_LED_SCROLLLOCK))
		hid |= 0x04;

	kbd.leds = hid;
	send_leds();
}

static void
kbd_alarm(clock_t stamp)
{
	expire_timers(stamp);
}

/*
 * Everything that is not the input protocol: the USB protocol, from xhci.
 * libusb panics on a message it does not know, so only the three it does
 * are handed to it.
 */
static void
kbd_other(message *m, int ipc_status)
{
	if (is_ipc_notify(ipc_status))
		return;

	if (m->m_source == usbd_ep && (m->m_type == USB_COMPLETE_URB ||
	    m->m_type == USB_ANNOUCE_DEV || m->m_type == USB_WITHDRAW_DEV)) {
		usb_handle_msg(&kbd_usb, m);
		return;
	}

	log_warn(&kbd_log, "unexpected message 0x%x from %d\n", m->m_type,
	    m->m_source);
}

static struct inputdriver kbd_tab = {
	.idr_leds	= kbd_leds,
	.idr_alarm	= kbd_alarm,
	.idr_other	= kbd_other
};

/*===========================================================================*
 *    typing without a keyboard                                              *
 *===========================================================================*/
/*
 * inject=TEXT: type TEXT as a keyboard would, a key down and up per
 * character, starting a second after the driver comes up.
 *
 * It exists to prove the half of the path that has nothing to do with
 * USB - report() and its order, libinputdriver, the input server, tty's
 * keymap and modifiers, the console line and its echo - where there is
 * no USB keyboard: on QEMU, which has no xHCI this port drives, and on
 * the board with nobody at it.  The characters are turned into boot
 * reports (a US layout: shift for capitals and the upper row of
 * symbols, control for the characters below space) and go through
 * report() like any keyboard's.  With no host controller running the
 * driver does only this.
 *
 * Arguments are split at spaces, so TEXT is URL-style: %20 is a space,
 * %0a is Enter, %25 is a percent sign.  %[MODS+KEY] is a chord - what no
 * character types, a compositor's keys first of all: MODS is any of C
 * (control), S (shift), A (alt) and G (the GUI key, Super), each followed
 * by '+', and KEY a character or one of Left Right Up Down Home End PageUp
 * PageDown Insert Delete Return Escape Tab Space Backspace Print F1..F12 -
 * so "%[G+Right]", "%[G+S+Left]", "%[G+r]", "%[Print]".  injectwait=N
 * waits N seconds instead of one - long enough, say, to log out and have
 * getty ask for a login name that is then typed.
 */
#define INJECT_MAX	256

/* A key and the modifier bits held with it: one report. */
struct keystroke {
	uint8_t mods;
	uint8_t key;
};

static struct keystroke inject[INJECT_MAX];
static size_t inject_len, inject_pos;
static int inject_down;			/* the current keystroke's key */
static minix_timer_t tmr_inject;

/* The keys of a chord that have names rather than characters. */
static const struct {
	const char *name;
	uint8_t key;
} key_names[] = {
	{ "Return", 0x28 }, { "Escape", 0x29 }, { "Backspace", 0x2a },
	{ "Tab", 0x2b }, { "Space", 0x2c }, { "Insert", 0x49 },
	{ "Home", 0x4a }, { "PageUp", 0x4b }, { "Delete", 0x4c },
	{ "End", 0x4d }, { "PageDown", 0x4e }, { "Right", 0x4f },
	{ "Left", 0x50 }, { "Down", 0x51 }, { "Up", 0x52 },
	{ "Print", 0x46 }
};

/* A character to a key code and the modifiers it needs. */
static int
char_key(int c, uint8_t *mods)
{
	static const char shifted_digit[] = "!@#$%^&*()";
	static const char sym[] = "-=[]\\;'`,./";
	static const char sym_shifted[] = "_+{}|:\"~<>?";
	static const uint8_t sym_key[] = {
		0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x33, 0x34, 0x35, 0x36, 0x37,
		0x38
	};
	const char *p;

	*mods = 0;
	if (c >= 'a' && c <= 'z')
		return KBD_KEY_FIRST + (c - 'a');
	if (c >= 'A' && c <= 'Z') {
		*mods = 0x02;			/* left shift */
		return KBD_KEY_FIRST + (c - 'A');
	}
	if (c >= '1' && c <= '9')
		return 0x1e + (c - '1');
	if (c == '0')
		return 0x27;
	switch (c) {
	case '\n':
	case '\r':	return 0x28;		/* Enter */
	case 033:	return 0x29;		/* Escape */
	case '\b':	return 0x2a;		/* Backspace */
	case '\t':	return 0x2b;		/* Tab */
	case ' ':	return 0x2c;
	case '\0':	return -1;
	}
	if ((p = strchr(shifted_digit, c)) != NULL) {
		*mods = 0x02;
		return 0x1e + (int)(p - shifted_digit);
	}
	if ((p = strchr(sym, c)) != NULL)
		return sym_key[p - sym];
	if ((p = strchr(sym_shifted, c)) != NULL) {
		*mods = 0x02;
		return sym_key[p - sym_shifted];
	}
	if (c >= 1 && c <= 26) {
		*mods = 0x01;			/* left control: ^A to ^Z */
		return KBD_KEY_FIRST + (c - 1);
	}
	return -1;
}

/*
 * A chord's text, between "%[" and "]", to a keystroke: modifier letters
 * each with a '+', then the key.  Answers -1 if it is not a chord.
 */
static int
chord_key(const char *s, size_t len, struct keystroke *k)
{
	char name[16], *end;
	uint8_t mods;
	unsigned i;
	long f;
	int key;

	k->mods = 0;
	while (len >= 2 && s[1] == '+') {
		switch (s[0]) {
		case 'C':	k->mods |= 0x01; break;	/* left control */
		case 'S':	k->mods |= 0x02; break;	/* left shift */
		case 'A':	k->mods |= 0x04; break;	/* left alt */
		case 'G':	k->mods |= 0x08; break;	/* left GUI */
		default:	return -1;
		}
		s += 2;
		len -= 2;
	}
	if (len == 0 || len >= sizeof(name))
		return -1;
	memcpy(name, s, len);
	name[len] = '\0';

	if (len == 1) {
		if ((key = char_key((unsigned char)name[0], &mods)) < 0)
			return -1;
		k->mods |= mods;
		k->key = (uint8_t)key;
		return 0;
	}
	for (i = 0; i < sizeof(key_names) / sizeof(key_names[0]); i++)
		if (strcmp(name, key_names[i].name) == 0) {
			k->key = key_names[i].key;
			return 0;
		}
	if (name[0] == 'F' && (f = strtol(name + 1, &end, 10)) >= 1 &&
	    f <= 12 && *end == '\0') {
		k->key = (uint8_t)(0x3a + f - 1);	/* F1 is 0x3a */
		return 0;
	}
	return -1;
}

static void
inject_step(int arg)
{
	uint8_t rep[KBD_REPORT_LEN];

	memset(rep, 0, sizeof(rep));

	if (inject_down) {
		report(rep, sizeof(rep));	/* everything up */
		inject_down = 0;
		inject_pos++;
	} else {
		if (inject_pos >= inject_len) {
			log_info(&kbd_log, "inject: typed %u key(s)\n",
			    (unsigned)inject_len);
			return;
		}
		rep[0] = inject[inject_pos].mods;
		rep[2] = inject[inject_pos].key;
		report(rep, sizeof(rep));
		inject_down = 1;
	}

	set_timer(&tmr_inject, 2, inject_step, 0);
}

static int
hexdigit(int c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

/*
 * The argument as keystrokes: characters, %XX escapes undone, and %[...]
 * chords.  What no key types is said and left out.  Answers how many.
 */
static size_t
inject_parse(const char *s, struct keystroke *out, size_t max)
{
	const char *end;
	size_t n = 0;
	uint8_t mods;
	int hi, lo, c, key;

	while (*s != '\0' && n < max) {
		if (s[0] == '%' && s[1] == '[' &&
		    (end = strchr(s + 2, ']')) != NULL) {
			if (chord_key(s + 2, (size_t)(end - (s + 2)),
			    &out[n]) == 0)
				n++;
			else
				log_warn(&kbd_log, "inject: no such chord "
				    "%%[%.*s]\n", (int)(end - (s + 2)), s + 2);
			s = end + 1;
			continue;
		}
		if (s[0] == '%' && (hi = hexdigit(s[1])) >= 0 &&
		    (lo = hexdigit(s[2])) >= 0) {
			c = hi * 16 + lo;
			s += 3;
		} else
			c = (unsigned char)*s++;
		if ((key = char_key(c, &mods)) < 0) {
			log_warn(&kbd_log, "inject: no key types 0x%02x\n", c);
			continue;
		}
		out[n].mods = mods;
		out[n].key = (uint8_t)key;
		n++;
	}
	return n;
}

/*===========================================================================*
 *    start and stop                                                         *
 *===========================================================================*/
static int
kbd_init(int type, sef_init_info_t *info)
{
	static char name[] = "usb_kbd";	/* libusb takes no const */
	char arg[INJECT_MAX];
	long v, delay = 500, rate = 30, inject_wait = 1;
	clock_t hz = (clock_t)sys_hz();
	int r;

	if (env_parse("log", "d", 0, &v, LEVEL_NONE, LEVEL_TRACE) == EP_SET)
		kbd_log.log_level = (int)v;
	(void)env_parse("delay", "d", 0, &delay, 100, 2000);
	(void)env_parse("rate", "d", 0, &rate, 2, 50);
	if (env_get_param("inject", arg, sizeof(arg)) == OK)
		inject_len = inject_parse(arg, inject, INJECT_MAX);
	(void)env_parse("injectwait", "d", 0, &inject_wait, 1, 600);

	rep_delay = (clock_t)(delay * hz / 1000);
	rep_period = (clock_t)(hz / rate);
	if (rep_delay < 1)
		rep_delay = 1;
	if (rep_period < 1)
		rep_period = 1;

	init_timer(&tmr_repeat);
	init_timer(&tmr_retry);
	init_timer(&tmr_inject);
	memset(&kbd, 0, sizeof(kbd));
	kbd.stage = ST_NONE;
	kbd.leds_sent = -1;

	/*
	 * The host controller first, and refused politely when it is not
	 * there: libusb's usb_init() panics, and a driver that panics at
	 * start is restarted by RS, panics again, and fills the console.
	 * Only typing from inject= needs none.
	 */
	if ((r = ds_retrieve_label_endpt("usbd", &usbd_ep)) != OK) {
		if (inject_len == 0) {
			log_warn(&kbd_log, "no USB host controller is running "
			    "(no \"usbd\": %d)\n", r);
			return r;
		}
		usbd_ep = NONE;
		log_info(&kbd_log, "no USB host controller; typing what "
		    "inject= says and nothing else\n");
	} else
		usb_init(name);

	inputdriver_announce(INPUT_DEV_KBD);

	/* A second for the input server to connect, then the typing. */
	if (inject_len != 0)
		set_timer(&tmr_inject, (clock_t)inject_wait * hz, inject_step,
		    0);

	if (usbd_ep != NONE)
		log_info(&kbd_log, "waiting for a keyboard (repeat after %ld "
		    "ms, %ld per second)\n", delay, rate);
	return OK;
}

/*
 * On the way out: let go of every key, and tell xhci this driver is gone,
 * so that the report still waiting on the ring is answered to nobody.
 */
static void
kbd_signal(int signo)
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

	sef_setcb_init_fresh(kbd_init);
	sef_setcb_signal_handler(kbd_signal);
	sef_startup();

	inputdriver_task(&kbd_tab);

	return 0;
}
