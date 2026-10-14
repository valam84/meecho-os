#include <minix/fb.h>
#include <minix/chardriver.h>
#include <minix/drivers.h>
#include <minix/ds.h>
#include <minix/sysutil.h>
#include <minix/type.h>
#include <minix/vm.h>
#include <sys/ioc_fb.h>
#include <assert.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <dev/videomode/videomode.h>
#include <dev/videomode/edidvar.h>
#include <dev/videomode/edidreg.h>

#include "fb_edid.h"
#include "fb.h"
#include "fbcon.h"

/*
 * Function prototypes for the fb driver.
 */
static int fb_open(devminor_t minor, int access, endpoint_t user_endpt);
static int fb_close(devminor_t minor);
static ssize_t fb_read(devminor_t minor, u64_t pos, endpoint_t ep,
	cp_grant_id_t gid, size_t size, int flags, cdev_id_t id);
static ssize_t fb_write(devminor_t minor, u64_t pos, endpoint_t ep,
	cp_grant_id_t gid, size_t size, int flags, cdev_id_t id);
static int fb_ioctl(devminor_t minor, unsigned long request, endpoint_t ep,
	cp_grant_id_t gid, int flags, endpoint_t user_ep, cdev_id_t id);
static int fb_init_display(int minor);
static void paint_testcard(int minor);
static int do_get_varscreeninfo(int minor, endpoint_t ep, cp_grant_id_t gid);
static int do_put_varscreeninfo(int minor, endpoint_t ep, cp_grant_id_t gid);
static int do_get_fixscreeninfo(int minor, endpoint_t ep, cp_grant_id_t gid);
static int do_pan_display(int minor, endpoint_t ep, cp_grant_id_t gid);
static int keep_displaying_restarted(void);
static void fb_alarm(clock_t stamp);
static void fb_other(message *m, int ipc_status);

/* Entry points to the fb driver. */
static struct chardriver fb_tab =
{
	.cdr_open	= fb_open,
	.cdr_close	= fb_close,
	.cdr_read	= fb_read,
	.cdr_write	= fb_write,
	.cdr_ioctl	= fb_ioctl,
	.cdr_alarm	= fb_alarm,
	.cdr_other	= fb_other
};

/*
 * The console on the screen (fbcon.c): on unless fbcon=0, and shown after
 * the test card has been up for card= seconds (10 by default; 0 shows the
 * console at once) - long enough to photograph the card on a boot.  The
 * kernel's messages are on it unless kmess=0.
 */
static long console_on = 1;
static long card_seconds = 10;
static long kmess_on = 1;

/** Represents the /dev/fb device. */
static int has_restarted = 0;
static u64_t has_restarted_t1, has_restarted_t2;

static int open_counter[FB_DEV_NR];		/* Open count */

static int
fb_open(devminor_t minor, int UNUSED(access), endpoint_t UNUSED(user_endpt))
{
	if (minor < 0 || minor >= FB_DEV_NR) return ENXIO;

	if (fb_init_display(minor) == OK) {
		/* A program takes the pixels over; the console waits. */
		if (open_counter[minor]++ == 0)
			fbcon_pause(1);
		return OK;
	}
	return ENXIO;
}

/*
 * Bring the display up and put the first picture on it, once.  Called when
 * the driver starts - so that a monitor shows something without anyone
 * opening the device - and again from every open, where it is a no-op once
 * it has worked.
 */
static int
fb_init_display(int minor)
{
	static int initialized = 0;
	static struct edid_info info;
	static struct edid_info *infop = NULL;
	int r;

	if (!initialized) {
		r = fb_edid_read(minor, &info);
		infop = (r == 0) ? &info : NULL;
	}

	if (arch_fb_init(minor, infop) != OK)
		return ENXIO;

	if (!initialized) {
		if (has_restarted)
			read_frclock_64(&has_restarted_t1);
		paint_testcard(minor);
		initialized = 1;
	}
	return OK;
}

static int
fb_close(devminor_t minor)
{
	if (minor < 0 || minor >= FB_DEV_NR) return ENXIO;
	assert(open_counter[minor] > 0);
	if (--open_counter[minor] == 0) {
		arch_fb_release(minor);	/* the screen back, then the console */
		fbcon_pause(0);
	}
	return OK;
}

/*
 * The first alarm ends the test card and attaches the console; every one
 * is the console's tick (fbcon_tick: the kernel's new messages, and one
 * drawing of whatever came since the last) and the arch part's (a monitor
 * plugged in after bring-up), and sets the next, a tenth of a second on.
 * A console that could not attach is not retried; the tick goes on.
 */
static void
fb_alarm(clock_t UNUSED(stamp))
{
	if (console_on && fbcon_attach(0, (int)kmess_on) != OK)
		console_on = 0;
	if (console_on)
		fbcon_tick();
	arch_fb_tick(0);
	(void)sys_setalarm(sys_hz() / 10, 0);
}

static void
fb_other(message *m, int ipc_status)
{
	/* tty's console grew (see fbcon.c). */
	if (is_ipc_notify(ipc_status) && m->m_source == TTY_PROC_NR)
		fbcon_pull();
}

static ssize_t
fb_read(devminor_t minor, u64_t pos, endpoint_t ep, cp_grant_id_t gid,
	size_t size, int UNUSED(flags), cdev_id_t UNUSED(id))
{
	struct device dev;
	int r;

	if (minor < 0 || minor >= FB_DEV_NR) return ENXIO;
	assert(open_counter[minor] > 0);

	arch_get_device(minor, &dev);

	if (size == 0 || pos >= dev.dv_size) return 0;
	if (pos + size > dev.dv_size)
		size = (size_t)(dev.dv_size - pos);

	r = sys_safecopyto(ep, gid, 0, (vir_bytes)(dev.dv_base + (size_t)pos),
	    size);

	return (r != OK) ? r : size;
}

static int
fb_ioctl(devminor_t minor, unsigned long request, endpoint_t ep,
	cp_grant_id_t gid, int UNUSED(flags), endpoint_t user_ep,
	cdev_id_t UNUSED(id))
{
/* Process I/O control requests */
	int r;

	if (minor < 0 || minor >= FB_DEV_NR) return ENXIO;

	switch(request) {
	case FBIOGET_VSCREENINFO:
		r = do_get_varscreeninfo(minor, ep, gid);
		return r;
	case FBIOPUT_VSCREENINFO:
		r = do_put_varscreeninfo(minor, ep, gid);
		return r;
	case FBIOGET_FSCREENINFO:
		r = do_get_fixscreeninfo(minor, ep, gid);
		return r;
	case FBIOPAN_DISPLAY:
		r = do_pan_display(minor, ep, gid);
		return r;
	}

	return arch_fb_ioctl(minor, request, ep, gid, user_ep);
}

static int
do_get_varscreeninfo(int minor, endpoint_t ep, cp_grant_id_t gid)
{
	int r;
	struct fb_var_screeninfo fbvs;

	if ((r = arch_get_varscreeninfo(minor, &fbvs)) == OK) {
		r = sys_safecopyto(ep, gid, 0, (vir_bytes) &fbvs, sizeof(fbvs));
	}

	return r;
}

static int
do_put_varscreeninfo(int minor, endpoint_t ep, cp_grant_id_t gid)
{
	int r;
	struct fb_var_screeninfo fbvs_copy;

	if (has_restarted && keep_displaying_restarted()) {
		return EAGAIN;
	}

	if ((r = sys_safecopyfrom(ep, gid, 0, (vir_bytes) &fbvs_copy,
	    sizeof(fbvs_copy))) != OK) {
		return r;
	}

	return arch_put_varscreeninfo(minor, &fbvs_copy);
}

static int
do_pan_display(int minor, endpoint_t ep, cp_grant_id_t gid)
{
	int r;
        struct fb_var_screeninfo fbvs_copy;

	if (has_restarted && keep_displaying_restarted()) {
		return EAGAIN;
	}

        if ((r = sys_safecopyfrom(ep, gid, 0, (vir_bytes) &fbvs_copy,
            sizeof(fbvs_copy))) != OK) {
                return r;
        }

        return arch_pan_display(minor, &fbvs_copy);
}

static int
do_get_fixscreeninfo(int minor, endpoint_t ep, cp_grant_id_t gid)
{
        int r;
        struct fb_fix_screeninfo fbfs;

        if ((r = arch_get_fixscreeninfo(minor, &fbfs)) == OK) {
                r = sys_safecopyto(ep, gid, 0, (vir_bytes) &fbfs, sizeof(fbfs));
        }

        return r;
}

static ssize_t
fb_write(devminor_t minor, u64_t pos, endpoint_t ep, cp_grant_id_t gid,
	size_t size, int UNUSED(flags), cdev_id_t UNUSED(id))
{
	struct device dev;
	int r;

	if (minor < 0 || minor >= FB_DEV_NR) return ENXIO;
	assert(open_counter[minor] > 0);

	if (has_restarted && keep_displaying_restarted())
		return EAGAIN;

	arch_get_device(minor, &dev);

	if (size == 0 || pos >= dev.dv_size) return 0;
	if (pos + size > dev.dv_size)
		size = (size_t)(dev.dv_size - pos);

	r = sys_safecopyfrom(ep, gid, 0,
	    (vir_bytes)(dev.dv_base + (size_t)pos), size);
	if (r == OK)
		arch_fb_flush(minor, (size_t)pos, size);

	return (r != OK) ? r : size;
}

static int
sef_cb_lu_state_save(int UNUSED(result), int UNUSED(flags))
{
/* Save the state. */
	ds_publish_u32("open_counter", open_counter[0], DSF_OVERWRITE);

	return OK;
}

static int
lu_state_restore() {
/* Restore the state. */
	u32_t value;

	ds_retrieve_u32("open_counter", &value);
	ds_delete_u32("open_counter");
	open_counter[0] = (int) value;

	return OK;
}

static int
sef_cb_init(int type, sef_init_info_t *UNUSED(info))
{
/* Initialize the fb driver. */
	int do_announce_driver = TRUE;

	open_counter[0] = 0;
	switch(type) {
	case SEF_INIT_FRESH:
	    printf("framebuffer fresh: pid %d\n", getpid());
	    break;

	case SEF_INIT_LU:
	    /* Restore the state. */
	    lu_state_restore();
	    do_announce_driver = FALSE;

	    printf("framebuffer: I'm a new version!\n");
	    break;

	case SEF_INIT_RESTART:
	    printf("framebuffer restarted: pid %d\n", getpid());
	    has_restarted = 1;
	    break;
	}

	/*
	 * A fresh or restarted driver lights the display at once.  One that
	 * finds no display says so and does not start: a frame buffer device
	 * that answers ENXIO to every open is no use to anybody.
	 */
	if (type != SEF_INIT_LU && fb_init_display(0) != OK)
		return ENXIO;

	/*
	 * The console replaces the test card after card= seconds.  The switch
	 * is fbcon=, not console=: env_parse() reads the boot parameters too
	 * when the service arguments do not name a key, console=tty00 is one
	 * of them, and a word where a number is expected is a panic - which
	 * is how the first run of this on the board ended.
	 */
	if (type != SEF_INIT_LU) {
		(void)env_parse("fbcon", "d", 0, &console_on, 0, 1);
		(void)env_parse("card", "d", 0, &card_seconds, 0, 3600);
		(void)env_parse("kmess", "d", 0, &kmess_on, 0, 1);
		(void)sys_setalarm(card_seconds > 0 ?
		    card_seconds * sys_hz() : 1, 0);
	}

	/* Announce we are up when necessary. */
	if (do_announce_driver) {
		chardriver_announce();
	}

	/* Initialization completed successfully. */
	return OK;
}

static void
sef_local_startup()
{
	/* Register init callbacks. Use the same function for all event types */
	sef_setcb_init_fresh(sef_cb_init);
	sef_setcb_init_lu(sef_cb_init);
	sef_setcb_init_restart(sef_cb_init);

	/* Register live update callbacks  */
	sef_setcb_lu_state_save(sef_cb_lu_state_save);

	/* Let SEF perform startup. */
	sef_startup();
}

int
main(int argc, char *argv[])
{
	env_setargs(argc, argv);
	fb_edid_args_parse();

	sef_local_startup();
	chardriver_task(&fb_tab);
	return OK;
}

static int
keep_displaying_restarted()
{
	u64_t delta;
	u32_t micro_delta;

	read_frclock_64(&has_restarted_t2);
	delta = delta_frclock_64(has_restarted_t1, has_restarted_t2);
	micro_delta = frclock_64_to_micros(delta);

#define DISPLAY_1SEC 1000000	/* 1 second in microseconds */
	if (micro_delta < DISPLAY_1SEC) {
		return 1;
	}

	has_restarted = 0;
	return 0;
}

/*
 * The first picture: a test card, drawn for 32-bit pixels in whatever
 * layout arch_get_varscreeninfo() reports.
 *
 * It is made to be read off a photograph of the screen.  Eight bars of full
 * colour - white, yellow, cyan, green, magenta, red, blue, black, left to
 * right - say whether red and blue are swapped and whether a channel is
 * missing; the grey ramp below them says whether all eight bits of each
 * arrive; the three ramps under it, red, green and blue, say which channel
 * is which on its own; and the white frame one pixel in from the edge says
 * whether the whole picture is on the screen or the monitor crops it.
 *
 * It replaces MINIX's boot and restart logos, which were the raccoon - the
 * old system's mark, which this one does not show.
 */
static void
paint_testcard(int minor)
{
	static const u8_t bars[8][3] = {
		{ 255, 255, 255 }, { 255, 255, 0 }, { 0, 255, 255 },
		{ 0, 255, 0 }, { 255, 0, 255 }, { 255, 0, 0 },
		{ 0, 0, 255 }, { 0, 0, 0 },
	};
	struct fb_var_screeninfo v;
	struct device dev;
	u32_t x, y, w, h, *row;
	u8_t r, g, b, level;

	if (arch_get_varscreeninfo(minor, &v) != OK || v.bits_per_pixel != 32 ||
	    arch_get_device(minor, &dev) != OK) {
		printf("fb: no 32-bit frame buffer to draw the test card on\n");
		return;
	}
	w = v.xres;
	h = v.yres;
	v.xoffset = v.yoffset = 0;
	(void)arch_pan_display(minor, &v);

	for (y = 0; y < h; y++) {
		row = (u32_t *)(vir_bytes)(dev.dv_base +
		    (u64_t)y * v.xres_virtual * 4);
		for (x = 0; x < w; x++) {
			level = (u8_t)(x * 256 / w);
			if (y < h * 2 / 3) {
				r = bars[x * 8 / w][0];
				g = bars[x * 8 / w][1];
				b = bars[x * 8 / w][2];
			} else if (y < h * 15 / 18) {
				r = g = b = level;
			} else if (y < h * 16 / 18) {
				r = level;
				g = b = 0;
			} else if (y < h * 17 / 18) {
				g = level;
				r = b = 0;
			} else {
				b = level;
				r = g = 0;
			}
			if (x == 1 || y == 1 || x == w - 2 || y == h - 2)
				r = g = b = 255;
			row[x] = (u32_t)r << v.red.offset |
			    (u32_t)g << v.green.offset |
			    (u32_t)b << v.blue.offset;
		}
	}
	arch_fb_flush(minor, 0, (size_t)v.xres_virtual * 4 * h);
}
