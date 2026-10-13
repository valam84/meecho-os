/*
 * The keyboard and the mouse: the input server's devices, as wlroots input
 * devices.
 *
 * The input server merges every keyboard into /dev/kbdmux and every mouse
 * into /dev/mousemux, and gives each reader struct input_event records -
 * HID usages, the keyboard page for keys, relative X, Y and wheel in the
 * general desktop page, buttons in the button page.  While this process
 * reads /dev/kbdmux the keys go here rather than to the console, and come
 * back to it when the compositor exits.
 *
 * Here they become one wlr_keyboard and one wlr_pointer, which tinywl's
 * code then treats as it treats any backend's: the keyboard gets an XKB
 * keymap and its key codes are evdev's (hid2evdev.c), the pointer moves
 * the cursor.  Two things are adjusted on the way:
 *
 *   usb_kbd repeats a held key itself, as a PC keyboard does; Wayland
 *   clients repeat by themselves from the compositor's repeat rate, so a
 *   press of a key that is already down is dropped here;
 *
 *   a mouse report is X, Y, the wheel and the buttons together, and the
 *   input server hands them over one event each: the motion of one read
 *   is summed into one motion event, and a frame event closes the read.
 */
#include <sys/types.h>

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <minix/input.h>

#include <linux/input-event-codes.h>
#include <wayland-server-core.h>
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/interfaces/wlr_pointer.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/util/log.h>

#include "meechowm.h"

static struct wlr_keyboard keyboard;
static struct wlr_pointer pointer;
static uint8_t key_down[256];

static const struct wlr_keyboard_impl keyboard_impl = {
	.name = "meecho-keyboard",
};

static const struct wlr_pointer_impl pointer_impl = {
	.name = "meecho-mouse",
};

static uint32_t
now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static int
keyboard_readable(int fd, uint32_t mask, void *data)
{
	struct input_event ev[32];
	struct wlr_keyboard_key_event k;
	ssize_t r;
	size_t i;
	uint32_t code;

	if ((r = read(fd, ev, sizeof(ev))) <= 0)
		return 0;
	for (i = 0; i < (size_t)r / sizeof(ev[0]); i++) {
		if (ev[i].page != INPUT_PAGE_KEY ||
		    (code = mc_hid_to_evdev(ev[i].code)) == KEY_RESERVED)
			continue;
		if (ev[i].value == INPUT_PRESS) {
			if (key_down[code & 0xff])
				continue;	/* usb_kbd's own repeat */
			key_down[code & 0xff] = 1;
		} else
			key_down[code & 0xff] = 0;

		memset(&k, 0, sizeof(k));
		k.time_msec = now_ms();
		k.keycode = code;
		k.update_state = true;
		k.state = ev[i].value == INPUT_PRESS ?
		    WL_KEYBOARD_KEY_STATE_PRESSED :
		    WL_KEYBOARD_KEY_STATE_RELEASED;
		wlr_keyboard_notify_key(&keyboard, &k);
	}
	return 0;
}

static void
button(unsigned hid, int pressed, uint32_t t)
{
	static const uint32_t map[] = { 0, BTN_LEFT, BTN_RIGHT, BTN_MIDDLE };
	struct wlr_pointer_button_event b;

	if (hid >= sizeof(map) / sizeof(map[0]) || map[hid] == 0)
		return;
	memset(&b, 0, sizeof(b));
	b.pointer = &pointer;
	b.time_msec = t;
	b.button = map[hid];
	b.state = pressed ? WL_POINTER_BUTTON_STATE_PRESSED :
	    WL_POINTER_BUTTON_STATE_RELEASED;
	wlr_pointer_notify_button(&pointer, &b);
}

static int
mouse_readable(int fd, uint32_t mask, void *data)
{
	struct input_event ev[32];
	struct wlr_pointer_motion_event m;
	struct wlr_pointer_axis_event a;
	ssize_t r;
	size_t i;
	int dx = 0, dy = 0, sent = 0;
	uint32_t t = now_ms();

	if ((r = read(fd, ev, sizeof(ev))) <= 0)
		return 0;
	wlr_log(WLR_DEBUG, "mouse: %zu event(s)", (size_t)r / sizeof(ev[0]));
	for (i = 0; i < (size_t)r / sizeof(ev[0]); i++) {
		if (ev[i].page == INPUT_PAGE_GD && ev[i].code == INPUT_GD_X)
			dx += ev[i].value;
		else if (ev[i].page == INPUT_PAGE_GD && ev[i].code == INPUT_GD_Y)
			dy += ev[i].value;
		else if (ev[i].page == INPUT_PAGE_GD &&
		    ev[i].code == INPUT_GD_WHEEL) {
			/* HID: away from the user is +1; Wayland: scrolling up
			 * is negative, 15 per notch, 120 per discrete step. */
			memset(&a, 0, sizeof(a));
			a.pointer = &pointer;
			a.time_msec = t;
			a.source = WL_POINTER_AXIS_SOURCE_WHEEL;
			a.orientation = WL_POINTER_AXIS_VERTICAL_SCROLL;
			a.relative_direction =
			    WL_POINTER_AXIS_RELATIVE_DIRECTION_IDENTICAL;
			a.delta = -15.0 * ev[i].value;
			a.delta_discrete = -120 * ev[i].value;
			wl_signal_emit_mutable(&pointer.events.axis, &a);
			sent = 1;
		} else if (ev[i].page == INPUT_PAGE_BUTTON) {
			button(ev[i].code, ev[i].value, t);
			sent = 1;
		}
	}
	if (dx != 0 || dy != 0) {
		memset(&m, 0, sizeof(m));
		m.pointer = &pointer;
		m.time_msec = t;
		m.delta_x = m.unaccel_dx = dx;
		m.delta_y = m.unaccel_dy = dy;
		wl_signal_emit_mutable(&pointer.events.motion, &m);
		sent = 1;
	}
	if (sent)
		wl_signal_emit_mutable(&pointer.events.frame, &pointer);
	return 0;
}

bool
mc_input_start(struct wl_event_loop *loop, mc_new_input_fn new_input,
	void *data)
{
	const char *kdev = getenv("MEECHOWM_KBD"), *mdev = getenv("MEECHOWM_MOUSE");
	int kfd, mfd;

	if (kdev == NULL)
		kdev = "/dev/kbdmux";
	if (mdev == NULL)
		mdev = "/dev/mousemux";

	if ((kfd = open(kdev, O_RDONLY | O_NONBLOCK | O_CLOEXEC)) < 0)
		wlr_log_errno(WLR_ERROR, "no keyboard: %s", kdev);
	else {
		wlr_keyboard_init(&keyboard, &keyboard_impl, "meecho-keyboard");
		wl_event_loop_add_fd(loop, kfd, WL_EVENT_READABLE,
		    keyboard_readable, NULL);
		new_input(data, &keyboard.base);
		wlr_log(WLR_INFO, "keyboard: %s", kdev);
	}

	if ((mfd = open(mdev, O_RDONLY | O_NONBLOCK | O_CLOEXEC)) < 0)
		wlr_log_errno(WLR_ERROR, "no mouse: %s", mdev);
	else {
		wlr_pointer_init(&pointer, &pointer_impl, "meecho-mouse");
		wl_event_loop_add_fd(loop, mfd, WL_EVENT_READABLE,
		    mouse_readable, NULL);
		new_input(data, &pointer.base);
		wlr_log(WLR_INFO, "mouse: %s", mdev);
	}
	return kfd >= 0 || mfd >= 0;
}
