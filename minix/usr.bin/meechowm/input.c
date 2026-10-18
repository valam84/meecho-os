/*
 * The keyboard, the mouse and a touch screen: the input server's devices,
 * as wlroots input devices.
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
 *   input server hands them over one event each, with no end of a report
 *   marked: mousesplit.c cuts a read back into reports, and each becomes
 *   one motion event, its wheel and buttons, and a frame event;
 *
 *   a touch screen comes with the mice, its contacts in the digitizer page
 *   and absolute X and Y (<minix/input.h>): touchsplit.c takes them out
 *   before mousesplit.c sees the read, and they become a wlr_touch.
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
#include <wlr/interfaces/wlr_touch.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_touch.h>
#include <wlr/util/log.h>

#include "meechowm.h"
#include "mousesplit.h"
#include "touchsplit.h"

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

/* The mouse's events, from mousesplit.c; arg is the time of the read. */
static void
mouse_motion(int dx, int dy, void *arg)
{
	struct wlr_pointer_motion_event m;

	memset(&m, 0, sizeof(m));
	m.pointer = &pointer;
	m.time_msec = *(uint32_t *)arg;
	m.delta_x = m.unaccel_dx = dx;
	m.delta_y = m.unaccel_dy = dy;
	wl_signal_emit_mutable(&pointer.events.motion, &m);
}

static void
mouse_wheel(int value, void *arg)
{
	struct wlr_pointer_axis_event a;

	/* HID: away from the user is +1; Wayland: scrolling up is negative,
	 * 15 per notch, 120 per discrete step. */
	memset(&a, 0, sizeof(a));
	a.pointer = &pointer;
	a.time_msec = *(uint32_t *)arg;
	a.source = WL_POINTER_AXIS_SOURCE_WHEEL;
	a.orientation = WL_POINTER_AXIS_VERTICAL_SCROLL;
	a.relative_direction = WL_POINTER_AXIS_RELATIVE_DIRECTION_IDENTICAL;
	a.delta = -15.0 * value;
	a.delta_discrete = -120 * value;
	wl_signal_emit_mutable(&pointer.events.axis, &a);
}

static void
mouse_button(unsigned hid, int pressed, void *arg)
{
	static const uint32_t map[] = { 0, BTN_LEFT, BTN_RIGHT, BTN_MIDDLE };
	struct wlr_pointer_button_event b;

	if (hid >= sizeof(map) / sizeof(map[0]) || map[hid] == 0)
		return;
	memset(&b, 0, sizeof(b));
	b.pointer = &pointer;
	b.time_msec = *(uint32_t *)arg;
	b.button = map[hid];
	b.state = pressed ? WL_POINTER_BUTTON_STATE_PRESSED :
	    WL_POINTER_BUTTON_STATE_RELEASED;
	wlr_pointer_notify_button(&pointer, &b);
}

static void
mouse_frame(void *arg)
{
	wl_signal_emit_mutable(&pointer.events.frame, &pointer);
}

static const struct mc_mouse_ops mouse_ops = {
	.motion = mouse_motion,
	.wheel = mouse_wheel,
	.button = mouse_button,
	.frame = mouse_frame,
};

/*
 * A touch screen, from touchsplit.c: a wlr_touch made when its first touch
 * comes - a machine without one has no touch in its seat, and its clients
 * are not told of one.
 */
static struct wlr_touch touch;
static bool touch_made;
static struct mc_touch touch_state;
static mc_new_input_fn touch_new_input;
static void *touch_new_input_data;

static const struct wlr_touch_impl touch_impl = {
	.name = "meecho-touch",
};

static void
touch_made_now(void)
{
	if (touch_made)
		return;
	touch_made = true;
	wlr_touch_init(&touch, &touch_impl, "meecho-touch");
	touch_new_input(touch_new_input_data, &touch.base);
	wlr_log(WLR_INFO, "touch screen: in the mice");
}

static void
touch_down(unsigned id, double x, double y, void *arg)
{
	struct wlr_touch_down_event d;

	touch_made_now();
	memset(&d, 0, sizeof(d));
	d.touch = &touch;
	d.time_msec = *(uint32_t *)arg;
	d.touch_id = (int32_t)id;
	d.x = x;
	d.y = y;
	wl_signal_emit_mutable(&touch.events.down, &d);
}

static void
touch_motion(unsigned id, double x, double y, void *arg)
{
	struct wlr_touch_motion_event m;

	memset(&m, 0, sizeof(m));
	m.touch = &touch;
	m.time_msec = *(uint32_t *)arg;
	m.touch_id = (int32_t)id;
	m.x = x;
	m.y = y;
	wl_signal_emit_mutable(&touch.events.motion, &m);
}

static void
touch_up(unsigned id, void *arg)
{
	struct wlr_touch_up_event u;

	memset(&u, 0, sizeof(u));
	u.touch = &touch;
	u.time_msec = *(uint32_t *)arg;
	u.touch_id = (int32_t)id;
	wl_signal_emit_mutable(&touch.events.up, &u);
}

static void
touch_frame(void *arg)
{
	wl_signal_emit_mutable(&touch.events.frame, NULL);
}

static const struct mc_touch_ops touch_ops = {
	.down = touch_down,
	.motion = touch_motion,
	.up = touch_up,
	.frame = touch_frame,
};

static int
mouse_readable(int fd, uint32_t mask, void *data)
{
	struct input_event ev[32];
	ssize_t r;
	size_t n;
	uint32_t t = now_ms();

	if ((r = read(fd, ev, sizeof(ev))) <= 0)
		return 0;
	wlr_log(WLR_DEBUG, "mouse: %zu event(s)", (size_t)r / sizeof(ev[0]));
	/* A touch screen's events out first; the mice's are left. */
	n = mc_touch_split(&touch_state, ev, (size_t)r / sizeof(ev[0]),
	    &touch_ops, &t);
	mc_mouse_split(ev, n, &mouse_ops, &t);
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

	touch_new_input = new_input;
	touch_new_input_data = data;
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
