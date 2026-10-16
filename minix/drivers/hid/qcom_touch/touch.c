/*
 * qcom_touch: a FocalTech FT5x06-family touch screen behind a Qualcomm QUP
 * I2C controller - the Mi Pad 4's - as the on-screen keyboard's keys.
 *
 * Found by the touch screen's compatible ("edt,edt-ft5406" and its kin) on
 * a "qcom,i2c-qup" bus; on any other machine it says it found none and
 * leaves.  Nothing Linux-like has set the hardware up, so the driver does it
 * from the tree itself (port/clover/hw-touch.md):
 *
 *   clocks        the bus's "clocks" - the QUP's core clock at the crystal,
 *                 19.2 MHz, and the BLSP bus clock - through the GCC
 *   pins          the bus's "pinctrl-0" (SDA/SCL to their I2C function),
 *                 the reset pin as an output, the interrupt pin as an input
 *                 with its pull-up - "reset-gpios" and "interrupts-extended"
 *                 of the touch screen's node
 *   controller    reset - low 6 ms, high, 300 ms - then identified
 *
 * Then it polls: fifty times a second it looks at the interrupt line, and
 * when the controller holds it low it reads a frame - up to five touches -
 * and hands each to keys.c, which turns touches on the keyboard <minix/osk.h>
 * draws into key presses for the input server.  Only with osk=1, the same
 * switch that makes fb draw the keyboard; without it the driver reports
 * touches in its log and sends nothing.
 *
 * The power rail (l11a, 1.8 V) is always on according to the tree, and
 * nothing here touches a regulator.
 */

#include <minix/drivers.h>
#include <minix/inputdriver.h>
#include <minix/fdt.h>
#include <minix/log.h>
#include <minix/sysutil.h>
#include <minix/vm.h>

#include <sys/mman.h>

#include <stdlib.h>
#include <string.h>

#include "qcom_soc.h"
#include "qup.h"
#include "keys.h"

static struct log tlog = {
	.name = "touch",
	.log_level = LEVEL_INFO,
	.log_func = default_log
};

#define POLL_HZ		50
#define FRAME_LEN	33		/* 3 header bytes + 5 touches of 6 */
#define MAX_TOUCH	5

static const char *const touch_compat[] = {
	"edt,edt-ft5406", "edt,edt-ft5306", "edt,edt-ft5506",
	"edt,edt-ft5x06", "edt,edt-ft6236", "focaltech,ft6236",
};

/* What the tree says. */
static struct {
	int found;
	u64_t bus_pa, bus_sz;
	unsigned clk_core, clk_iface;
	uint32_t pinctrl;
	unsigned addr;
	unsigned reset_pin, int_pin;
	int have_reset, have_int;
	unsigned size_x, size_y;
	int swap, inv_x, inv_y;
	unsigned scr_w, scr_h;
} dt;

static struct qup qup;
static volatile uint8_t *qup_regs;
static struct keys keys;
static int osk_on, ev_ft;
static unsigned active;			/* touch ids in the last frame */
static unsigned long frames, errors;

/*===========================================================================*
 *    the tree                                                                *
 *===========================================================================*/
static unsigned
cell(const struct fdt_node *n, const char *prop, unsigned i, unsigned dflt)
{
	const void *p;
	unsigned len;

	if ((p = fdt_getprop(n, prop, &len)) == NULL || len < 4 * (i + 1))
		return dflt;
	return (unsigned)fdt_read_cells((const char *)p + 4 * i, 1);
}

static int
is_touch(const struct fdt_node *n)
{
	unsigned i;

	for (i = 0; i < sizeof(touch_compat) / sizeof(touch_compat[0]); i++)
		if (fdt_node_is_compatible(n, touch_compat[i]))
			return 1;
	return 0;
}

struct walk {
	int bus_depth;			/* -1: not inside a QUP bus */
	u64_t pa, sz;
	unsigned clk[2], nclk;
	uint32_t pinctrl;
};

static int
find_cb(void *cookie, int depth, const char *UNUSED(name),
	const struct fdt_node *n)
{
	struct walk *w = cookie;
	const char *s, *p;
	unsigned len, i;

	if (w->bus_depth >= 0 && depth <= w->bus_depth)
		w->bus_depth = -1;

	if (fdt_node_is_compatible(n, "simple-framebuffer")) {
		dt.scr_w = cell(n, "width", 0, 0);
		dt.scr_h = cell(n, "height", 0, 0);
	}

	if (fdt_node_is_compatible(n, "qcom,i2c-qup-v2.2.1") ||
	    fdt_node_is_compatible(n, "qcom,i2c-qup-v2.1.1")) {
		if ((s = fdt_getprop(n, "status", &len)) != NULL && len > 0 &&
		    strcmp(s, "okay") != 0 && strcmp(s, "ok") != 0)
			return 0;
		w->bus_depth = depth;
		w->pa = w->sz = 0;
		(void)fdt_node_reg(n, 0, &w->pa, &w->sz);
		/* clocks = <&gcc core>, <&gcc iface>, by clock-names. */
		w->clk[0] = w->clk[1] = 0;
		if ((s = fdt_getprop(n, "clock-names", &len)) != NULL)
			for (p = s, i = 0; p < s + len && i < 2;
			    p += strlen(p) + 1, i++) {
				if (strcmp(p, "core") == 0)
					w->clk[0] = cell(n, "clocks", 2 * i + 1,
					    0);
				else if (strcmp(p, "iface") == 0)
					w->clk[1] = cell(n, "clocks", 2 * i + 1,
					    0);
			}
		w->pinctrl = cell(n, "pinctrl-0", 0, 0);
		return 0;
	}

	if (w->bus_depth >= 0 && depth == w->bus_depth + 1 && is_touch(n) &&
	    !dt.found) {
		if ((s = fdt_getprop(n, "status", &len)) != NULL && len > 0 &&
		    strcmp(s, "okay") != 0 && strcmp(s, "ok") != 0)
			return 0;
		dt.found = 1;
		dt.bus_pa = w->pa;
		dt.bus_sz = w->sz;
		dt.clk_core = w->clk[0];
		dt.clk_iface = w->clk[1];
		dt.pinctrl = w->pinctrl;
		dt.addr = cell(n, "reg", 0, 0);
		/* reset-gpios = <&tlmm 66 flags>; interrupts-extended too. */
		if (fdt_getprop(n, "reset-gpios", &len) != NULL && len >= 8) {
			dt.reset_pin = cell(n, "reset-gpios", 1, 0);
			dt.have_reset = 1;
		}
		if (fdt_getprop(n, "interrupts-extended", &len) != NULL &&
		    len >= 8) {
			dt.int_pin = cell(n, "interrupts-extended", 1, 0);
			dt.have_int = 1;
		}
		dt.size_x = cell(n, "touchscreen-size-x", 0, 0);
		dt.size_y = cell(n, "touchscreen-size-y", 0, 0);
		dt.swap = fdt_getprop(n, "touchscreen-swapped-x-y", &len) !=
		    NULL;
		dt.inv_x = fdt_getprop(n, "touchscreen-inverted-x", &len) !=
		    NULL;
		dt.inv_y = fdt_getprop(n, "touchscreen-inverted-y", &len) !=
		    NULL;
	}
	return 0;
}

/*===========================================================================*
 *    registers                                                               *
 *===========================================================================*/
static uint32_t
mmio_rd(void *ctx, uint32_t off)
{
	(void)ctx;
	return *(volatile uint32_t *)(qup_regs + off);
}

static void
mmio_wr(void *ctx, uint32_t off, uint32_t v)
{
	(void)ctx;
	*(volatile uint32_t *)(qup_regs + off) = v;
}

static void
delay_us(unsigned us)
{
	(void)micro_delay(us);
}

static int
reg_read(uint8_t reg, uint8_t *buf, unsigned n)
{
	return qup_read_reg(&qup, dt.addr, reg, buf, n);
}

/*===========================================================================*
 *    bring-up                                                                *
 *===========================================================================*/
static int
bring_up(void)
{
	void *dtb;
	struct walk w;
	uint8_t id[22], v;
	int r;

	if ((dtb = fdt_fetch()) == NULL)
		return ENXIO;
	memset(&w, 0, sizeof(w));
	w.bus_depth = -1;
	fdt_walk(dtb, find_cb, &w);
	if (!dt.found || dt.bus_sz == 0 || dt.addr == 0 || dt.addr > 0x7f) {
		log_info(&tlog, "no FT5x06 touch screen on a QUP I2C bus in "
		    "the tree; nothing to do\n");
		free(dtb);
		return ENXIO;
	}
	log_info(&tlog, "FT5x06 at 0x%02x on the QUP at 0x%llx; clocks %u/%u, "
	    "reset gpio%u, interrupt gpio%u, %ux%u%s\n", dt.addr,
	    (unsigned long long)dt.bus_pa, dt.clk_core, dt.clk_iface,
	    dt.reset_pin, dt.int_pin, dt.size_x, dt.size_y,
	    dt.swap ? ", swapped" : "");

	if ((r = qsoc_init(dtb)) != OK) {
		log_warn(&tlog, "the SoC's TLMM and GCC are not ones this "
		    "driver knows, or not granted (%d)\n", r);
		free(dtb);
		return r;
	}

	/* Clocks: the bus clock, then the core clock at the crystal. */
	if (dt.clk_iface != 0 && (r = qsoc_clk_enable(dt.clk_iface, 0)) != OK)
		log_warn(&tlog, "bus clock %u: %d\n", dt.clk_iface, r);
	if ((r = qsoc_clk_enable(dt.clk_core, 19200000)) != OK) {
		log_warn(&tlog, "core clock %u would not start: %d\n",
		    dt.clk_core, r);
		free(dtb);
		return r;
	}

	/* Pins. */
	if (dt.pinctrl != 0 && (r = qsoc_pinctrl(dtb, dt.pinctrl)) != OK)
		log_warn(&tlog, "bus pins (phandle %u): %d\n", dt.pinctrl, r);
	free(dtb);
	if (dt.have_int)
		(void)qsoc_pin(dt.int_pin, 0, 8, QSOC_PULL_UP, 0);
	if (dt.have_reset) {
		qsoc_pin_set(dt.reset_pin, 0);		/* value first */
		(void)qsoc_pin(dt.reset_pin, 0, 8, QSOC_PULL_UP, 1);
	}

	/* The controller. */
	if ((qup_regs = vm_map_phys(SELF, (void *)(phys_bytes)dt.bus_pa,
	    (size_t)dt.bus_sz)) == MAP_FAILED) {
		log_warn(&tlog, "cannot map the QUP; not granted by RS?\n");
		return EPERM;
	}
	memset(&qup, 0, sizeof(qup));
	qup.rd = mmio_rd;
	qup.wr = mmio_wr;
	qup.delay_us = delay_us;
	qup.src_hz = 19200000;
	qup.bus_hz = 400000;
	if ((r = qup_init(&qup)) != QUP_OK) {
		log_warn(&tlog, "QUP would not start: %d\n", r);
		return r;
	}
	log_info(&tlog, "QUP version 0x%08x, FIFOs %u/%u bytes, clock word "
	    "0x%08x\n", qup.hw_version, qup.out_fifo, qup.in_fifo,
	    qup.clk_ctl);

	/* Reset the touch controller: low 6 ms, high, 300 ms. */
	if (dt.have_reset) {
		micro_delay(6000);
		qsoc_pin_set(dt.reset_pin, 1);
		micro_delay(300000);
	}

	/* Who it is: EDT's "EP0..." id, or FocalTech's chip id at 0xa8. */
	if ((r = reg_read(0xbb, id, sizeof(id))) != QUP_OK) {
		log_warn(&tlog, "no answer from 0x%02x: %d\n", dt.addr, r);
		return r;
	}
	if ((id[1] == 'E' || id[1] == 'e') && (id[2] == 'P' || id[2] == 'p') &&
	    id[3] == '0') {
		log_warn(&tlog, "an EDT M06 controller: a protocol this driver "
		    "does not speak\n");
		return ENODEV;
	}
	if (reg_read(0xa8, &v, 1) == QUP_OK) {
		ev_ft = (v == 0x59);
		log_info(&tlog, "FocalTech chip id 0x%02x%s\n", v,
		    ev_ft ? " (EV-FT: x and y swapped)" : "");
	}
	(void)reg_read(0x00, &v, 1);
	return OK;
}

/*===========================================================================*
 *    frames                                                                  *
 *===========================================================================*/
static void
emit(unsigned usage, int down)
{
	inputdriver_send_event(FALSE, INPUT_PAGE_KEY, usage,
	    down ? INPUT_PRESS : INPUT_RELEASE, 0);
}

/* Raw coordinates to screen pixels, as the tree's properties say. */
static void
to_screen(unsigned *x, unsigned *y)
{
	unsigned t, sx = dt.size_x ? dt.size_x : keys.w,
	    sy = dt.size_y ? dt.size_y : keys.h;

	if (ev_ft != dt.swap) {
		t = *x;
		*x = *y;
		*y = t;
	}
	if (dt.inv_x && *x < sx)
		*x = sx - 1 - *x;
	if (dt.inv_y && *y < sy)
		*y = sy - 1 - *y;
	if (sx != keys.w)
		*x = *x * keys.w / sx;
	if (sy != keys.h)
		*y = *y * keys.h / sy;
}

static void
read_frame(void)
{
	uint8_t f[FRAME_LEN];
	unsigned i, seen = 0, ev, x, y, tid;
	int r;

	if ((r = reg_read(0x00, f, sizeof(f))) != QUP_OK) {
		if (errors++ < 5)
			log_warn(&tlog, "frame read failed: %d\n", r);
		return;
	}
	frames++;
	for (i = 0; i < MAX_TOUCH; i++) {
		const uint8_t *b = f + 3 + 6 * i;

		ev = b[0] >> 6;
		tid = b[2] >> 4;
		if (ev == 3 || tid >= MAX_TOUCH)
			continue;
		x = (unsigned)(b[0] & 0x0f) << 8 | b[1];
		y = (unsigned)(b[2] & 0x0f) << 8 | b[3];
		to_screen(&x, &y);
		if (ev != 1)
			seen |= 1u << tid;
		log_debug(&tlog, "touch %u: %s at %u,%u\n", tid,
		    ev == 0 ? "down" : ev == 1 ? "up" : "contact", x, y);
		if (osk_on)
			keys_touch(&keys, tid, (int)ev, x, y);
	}
	for (i = 0; i < MAX_TOUCH; i++)
		if ((active & (1u << i)) && !(seen & (1u << i)) && osk_on)
			keys_lost(&keys, i);
	active = seen;
}

static void
arm(void)
{
	(void)sys_setalarm(sys_hz() / POLL_HZ > 0 ? sys_hz() / POLL_HZ : 1, 0);
}

static void
touch_alarm(clock_t UNUSED(stamp))
{
	/*
	 * The controller holds its line low while it has a frame; without a
	 * line in the tree, every poll reads one.  A finger still down keeps
	 * the frames coming, and the last one, with every touch up, is read
	 * because the line was low for it too.
	 */
	if (!dt.have_int || !qsoc_pin_get(dt.int_pin) || active != 0)
		read_frame();
	arm();
}

static struct inputdriver touch_tab = {
	.idr_alarm	= touch_alarm
};

static int
touch_init(int UNUSED(type), sef_init_info_t *UNUSED(info))
{
	long v;
	int r;

	if (env_parse("log", "d", 0, &v, LEVEL_NONE, LEVEL_TRACE) == EP_SET)
		tlog.log_level = (int)v;
	v = 0;
	(void)env_parse("osk", "d", 0, &v, 0, 1);
	osk_on = (int)v;
	if ((r = bring_up()) != OK)
		return r;
	keys_init(&keys, dt.scr_w ? dt.scr_w : dt.size_x,
	    dt.scr_h ? dt.scr_h : dt.size_y, emit);
	if (keys.w == 0 || keys.h == 0) {
		log_warn(&tlog, "no screen size in the tree\n");
		return EINVAL;
	}
	log_info(&tlog, "polling %u times a second; the on-screen keyboard "
	    "%s\n", POLL_HZ, osk_on ? "is live" : "is off (osk=0): touches "
	    "only logged");
	inputdriver_announce(INPUT_DEV_KBD);
	arm();
	return OK;
}

static void
touch_signal(int signo)
{
	if (signo == SIGTERM)
		inputdriver_terminate();
}

int
main(int argc, char **argv)
{
	env_setargs(argc, argv);
	sef_setcb_init_fresh(touch_init);
	sef_setcb_signal_handler(touch_signal);
	sef_startup();
	inputdriver_task(&touch_tab);
	return 0;
}
