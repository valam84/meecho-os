/*
 * Keys, for the console line.
 *
 * There is no keyboard controller on an AArch64 machine for tty to drive;
 * a keyboard here is a USB device behind its own driver (usb_kbd), which
 * hands key events to the input server, which hands them here when no
 * program reads /dev/kbdmux.  That is the same path a PC keyboard takes on
 * i386 (pckbd -> input -> tty), and the events are the same: a key of the
 * USB HID keyboard page went down or came up.  So the half of i386's
 * keyboard.c that turns those events into characters - the keymap, shift,
 * control, alt, the lock keys and their LEDs, the escape sequences of the
 * cursor and function keys - is what this file is, and the keymap is the
 * same file (keymaps/us-std.src), loadable the same way.
 *
 * What is different is where the characters go.  On i386 they go to the
 * virtual console on the screen, which tty draws.  Here the console IS a
 * serial line (rs232.c), and the screen shows a copy of that line
 * (console.c, cons_mirror); so the characters are fed into the console
 * line's input as if they had arrived on the wire.  The keyboard and the
 * serial line then type into one terminal: getty, login and the shell
 * cannot tell them apart, what is typed is echoed to both the wire and
 * the screen, and a session can be started at one and continued at the
 * other.
 *
 * What i386 has and this does not: virtual consoles to switch between
 * (there is one console), function keys as debug-dump triggers (nothing
 * here watches them - they send their escape sequences, as i386 does
 * with debug_fkeys=0), and sticky alt.  Ctrl-Alt-Del is kept: three times
 * reboots, once asks init to.
 */
#include <minix/drivers.h>
#include <minix/ds.h>
#include <minix/input.h>
#include <minix/ipc.h>
#include <minix/keymap.h>
#include <sys/reboot.h>
#include <sys/termios.h>
#include <signal.h>
#include "tty.h"

static u16_t keymap[NR_SCAN_CODES][MAP_COLS] = {
#include "keymaps/us-std.src"
};

static endpoint_t input_endpt = NONE;

static int alt_l, alt_r, alt;		/* alt keys, and either */
static int ctrl_l, ctrl_r, ctrl;	/* control keys, and either */
static int shift_l, shift_r, shift;	/* shift keys, and either */
static int num_down, caps_down, scroll_down;	/* lock keys held */
static int locks;			/* lock key state: the LED bits */

/* Lock key bits, chosen equal to the input server's LED mask bits. */
#define SCROLL_LOCK	(1 << INPUT_LED_SCROLLLOCK)
#define NUM_LOCK	(1 << INPUT_LED_NUMLOCK)
#define CAPS_LOCK	(1 << INPUT_LED_CAPSLOCK)

/* The last letter of ESC [ x, for the keys from HOME to INSRT. */
static const char numpad_map[12] =
	{'H', 'Y', 'A', 'B', 'D', 'C', 'V', 'U', 'G', 'S', 'T', '@'};

/* ESC [ nn ~, for F1 to F12. */
static const char *const fkey_map[12] =
	{"11", "12", "13", "14", "15", "17",	/* F1-F6 */
	 "18", "19", "20", "21", "23", "24"};	/* F7-F12 */

/*
 * A key code to what the keymap says it is, given the modifiers held:
 * the same columns as on i386 (plain, shift, alt, AltGr, alt+shift,
 * control).
 */
static unsigned
map_key(int scode)
{
	int caps, column;
	u16_t *keyrow;

	keyrow = keymap[scode];

	caps = shift;
	if ((locks & NUM_LOCK) && (keyrow[0] & HASNUM))
		caps = !caps;
	if ((locks & CAPS_LOCK) && (keyrow[0] & HASCAPS))
		caps = !caps;

	if (alt) {
		column = 2;
		if (ctrl || alt_r)
			column = 3;	/* Ctrl + Alt == AltGr */
		if (caps)
			column = 4;
	} else {
		column = 0;
		if (caps)
			column = 1;
		if (ctrl)
			column = 5;
	}
	return keyrow[column] & ~(HASNUM | HASCAPS);
}

/* Make the input server set the LEDs to the state of the lock keys. */
static void
set_leds(void)
{
	message m;
	int r;

	if (input_endpt == NONE)
		return;

	memset(&m, 0, sizeof(m));
	m.m_type = INPUT_SETLEDS;
	m.m_input_linputdriver_setleds.led_mask = locks;

	if ((r = asynsend3(input_endpt, &m, AMF_NOREPLY)) != OK)
		printf("TTY: asynsend to INPUT %u failed (%d)\n", input_endpt,
		    r);
}

/*
 * A key went down or came up: keep the modifiers and the locks, and
 * answer what it types - a character up to 0xFF, a keymap code above
 * that, or -1 for nothing.
 */
static int
make_break(int scode, int make)
{
	static int cad_count = 0;
	int ch;

	/* Ctrl-Alt-Del: init reboots on the first, the third does. */
	if (make && ctrl && alt &&
	    (scode == INPUT_KEY_DELETE || scode == INPUT_KEY_INSERT)) {
		if (++cad_count == 3) {
			cons_stop();
			sys_abort(RB_AUTOBOOT);
		}
		sys_kill(INIT_PROC_NR, SIGABRT);
		return -1;
	}

	ch = map_key(scode);

	switch (ch) {
	case LCTRL:
	case RCTRL:
		*(ch == RCTRL ? &ctrl_r : &ctrl_l) = make;
		ctrl = ctrl_l | ctrl_r;
		return -1;
	case LSHIFT:
	case RSHIFT:
		*(ch == RSHIFT ? &shift_r : &shift_l) = make;
		shift = shift_l | shift_r;
		return -1;
	case LALT:
	case RALT:
		*(ch == RALT ? &alt_r : &alt_l) = make;
		alt = alt_l | alt_r;
		return -1;
	case CALOCK:			/* toggles on the way down */
		if (caps_down < make) {
			locks ^= CAPS_LOCK;
			set_leds();
		}
		caps_down = make;
		return -1;
	case NLOCK:
		if (num_down < make) {
			locks ^= NUM_LOCK;
			set_leds();
		}
		num_down = make;
		return -1;
	case SLOCK:
		if (scroll_down < make) {
			locks ^= SCROLL_LOCK;
			set_leds();
		}
		scroll_down = make;
		return -1;
	default:			/* a key that types */
		if (!make || ch == 0)
			return -1;	/* a release, or a key with no meaning */
		return ch;
	}
}

/*
 * What a key types, into the console line's input - the same entry point
 * the serial line's own receiver uses (rs_read() -> in_process()), so
 * that canonical editing, signals, echo and the waiting reader are all
 * the line's business from here on.
 */
static void
kb_key(int scode, int make)
{
	char buf[8], *p, suffix;
	tty_t *tp;
	int ch;

	if ((ch = make_break(scode, make)) < 0)
		return;

	if ((tp = line2tty(consoleline)) == NULL)
		return;

	if (ch <= 0xFF) {
		buf[0] = (char)ch;
		p = &buf[1];
	} else if (HOME <= ch && ch <= INSRT) {
		/* The cursor and editing keys: ESC [ x. */
		buf[0] = ESC;
		buf[1] = '[';
		buf[2] = numpad_map[ch - HOME];
		p = &buf[3];
	} else if ((F1 <= ch && ch <= F12) || (SF1 <= ch && ch <= SF12) ||
	    (CF1 <= ch && ch <= CF12)) {
		/* ^[[11~ for F1, ^[[24;5~ for Ctrl-F12 and so on. */
		if (F1 <= ch && ch <= F12) {
			ch -= F1;
			suffix = 0;
		} else if (SF1 <= ch && ch <= SF12) {
			ch -= SF1;
			suffix = '2';
		} else {
			ch -= CF1;
			suffix = shift ? '6' : '5';
		}
		buf[0] = ESC;
		buf[1] = '[';
		buf[2] = fkey_map[ch][0];
		buf[3] = fkey_map[ch][1];
		p = &buf[4];
		if (suffix) {
			*p++ = ';';
			*p++ = suffix;
		}
		*p++ = '~';
	} else {
		/*
		 * Alt with a function or cursor key: switching virtual
		 * consoles on i386, and there is one console here.
		 */
		return;
	}

	(void)in_process(tp, buf, (int)(p - buf));
	tp->tty_events = 1;
}

void
do_input(message *msg)
{
	endpoint_t endpt;
	unsigned short scode;
	int r;

	switch (msg->m_type) {
	case TTY_INPUT_UP:
		if ((r = ds_retrieve_label_endpt("input", &endpt)) != OK) {
			printf("TTY: unable to retrieve INPUT endpoint (%d)\n",
			    r);
			return;
		}
		if (endpt != msg->m_source) {
			printf("TTY: up request from non-INPUT %u\n",
			    msg->m_source);
			return;
		}

		input_endpt = msg->m_source;

		/* Pass the current state of the LEDs to INPUT. */
		set_leds();
		break;

	case TTY_INPUT_EVENT:
		if (msg->m_source != input_endpt) {
			printf("TTY: input event from non-INPUT %u\n",
			    msg->m_source);
			return;
		}

		/* Only keyboard keys, and only those the keymap has. */
		if (msg->m_input_tty_event.page != INPUT_PAGE_KEY)
			return;
		scode = msg->m_input_tty_event.code;
		if (scode >= NR_SCAN_CODES)
			return;

		kb_key(scode, msg->m_input_tty_event.value != INPUT_RELEASE);
		break;

	default:
		panic("do_input called for unknown message type %x",
		    msg->m_type);
	}
}

/*
 * Nothing watches the function keys here, and the request is answered
 * as such; a caller that was never answered would wait for ever.
 */
void
do_fkey_ctl(message *m)
{
	int r;

	m->m_type = ENOSYS;
	if ((r = ipc_sendnb(m->m_source, m)) != OK)
		printf("TTY: unable to reply to %d: %d\n", m->m_source, r);
}

void
kb_init_once(void)
{
}

/* Load a new keymap - the same format as i386's, from loadkeys(1). */
int
kbd_loadmap(endpoint_t endpt, cp_grant_id_t grant)
{
	return sys_safecopyfrom(endpt, grant, 0, (vir_bytes)keymap,
	    sizeof(keymap));
}

/*
 * The console ttys (ttyc0..) stay without a reader on purpose: they have
 * no screen behind them to write to, and a tty with a reader and no
 * writer is one that handle_events() would call through a NULL pointer.
 * Keys go to the console line instead (kb_key()).
 */
void
kb_init(tty_t *tp)
{
}
