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
 * Then it polls: fifty times a second it reads the controller's touch
 * count, and when there are touches it reads a frame - up to five - and
 * hands each to keys.c, where the console's keyboard (libosk,
 * <minix/libosk.h>) turns them into key presses for the input server.  Only
 * with osk=1, the same switch that makes fb draw the keyboard; without it
 * the driver reports touches in its log and sends nothing.
 *
 * The driver owns the keyboard - its layer, its modifiers, the key held -
 * and fb draws it, so the two talk through DS: every change goes out as
 * osk_word() under OSK_DS_STATE, and fb says under OSK_DS_CONSOLE whether
 * its console and keyboard are on the screen at all.  Under the test card
 * or a compositor, touches are not keys.
 *
 * inject=ITEM,ITEM,... touches the keyboard without a touch screen (QEMU,
 * port/test/osk/qemu-osk.sh), once the keyboard is on the screen: an item
 * is a key's label on the layer shown then (Space and Comma for those two),
 * tapped; +LABEL presses it and keeps it down; ~N waits N fiftieths.
 *
 * The power rail (l11a, 1.8 V) is always on according to the tree, and
 * nothing here touches a regulator.
 */

#include <minix/drivers.h>
#include <minix/ds.h>
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

/* The bus's own pins, from its pinctrl-0: SDA and SCL in the tree's order. */
static unsigned bus_pin[2], nbus;

static int
bus_line(unsigned i)
{
	return i < nbus ? qsoc_pin_get(bus_pin[i]) : -1;
}

static struct qup qup;
static volatile uint8_t *qup_regs;
static struct keys keys;
static int osk_on, ev_ft;
static int hw;				/* a touch screen, not only inject= */
static char inject[256];		/* inject=, what is left of it */
static char *inject_at;
static unsigned inject_wait, inject_held;
static int inject_tap;			/* touch 0 is down: up next tick */
static unsigned active;			/* touch ids in the last frame */
static unsigned long frames, errors, polls, int_low;
static unsigned shown;			/* touches written to the log */
static unsigned chip_id;		/* FocalTech id at 0xa8 */

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
	log_debug(&tlog, "FT5x06 at 0x%02x on the QUP at 0x%llx; clocks %u/%u, "
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

	/*
	 * The lines as the boot loader left them, before anything here is
	 * touched.  SDA and SCL are pulled up to the touch screen's 1.8 V
	 * supply: both low says the supply is off, not the driver wrong.
	 */
	nbus = dt.pinctrl != 0 ? qsoc_pinctrl_pins(dtb, dt.pinctrl, bus_pin,
	    2) : 0;
	log_debug(&tlog, "lines as found: SDA %d SCL %d, reset %d, interrupt "
	    "%d\n", bus_line(0), bus_line(1),
	    dt.have_reset ? qsoc_pin_get(dt.reset_pin) : -1,
	    dt.have_int ? qsoc_pin_get(dt.int_pin) : -1);

	/* Pins: the bus, and the interrupt as an input.  Reset not yet. */
	if (dt.pinctrl != 0 && (r = qsoc_pinctrl(dtb, dt.pinctrl)) != OK)
		log_warn(&tlog, "bus pins (phandle %u): %d\n", dt.pinctrl, r);
	free(dtb);
	if (dt.have_int)
		(void)qsoc_pin(dt.int_pin, 0, 8, QSOC_PULL_UP, 0);

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
	log_debug(&tlog, "QUP version 0x%08x, FIFOs %u/%u bytes, clock word "
	    "0x%08x\n", qup.hw_version, qup.out_fifo, qup.in_fifo,
	    qup.clk_ctl);

	/*
	 * First as the boot loader left the controller - running, if the
	 * panel is - and only then through a reset (low 6 ms, high, then
	 * waits of 300 ms growing to two seconds).  On the Mi Pad 4 the
	 * address did not answer after the reset, so each step says what it
	 * saw, and a controller that never answers ends in a scan of the bus.
	 */
	r = reg_read(0xa8, &v, 1);
	log_debug(&tlog, "before reset: 0x%02x answers %d (chip id 0x%02x), "
	    "SDA %d SCL %d\n", dt.addr, r, r == QUP_OK ? v : 0,
	    bus_line(0), bus_line(1));
	if (r != QUP_OK && dt.have_reset) {
		static const unsigned waits_ms[] = { 300, 300, 400, 1000 };
		unsigned i;

		qsoc_pin_set(dt.reset_pin, 0);		/* value first */
		(void)qsoc_pin(dt.reset_pin, 0, 8, QSOC_PULL_UP, 1);
		micro_delay(6000);
		qsoc_pin_set(dt.reset_pin, 1);
		for (i = 0; i < sizeof(waits_ms) / sizeof(waits_ms[0]); i++) {
			micro_delay(waits_ms[i] * 1000);
			if ((r = reg_read(0xa8, &v, 1)) == QUP_OK)
				break;
		}
		log_debug(&tlog, "after reset: 0x%02x answers %d (try %u), "
		    "reset line %d, interrupt %d\n", dt.addr, r, i + 1,
		    qsoc_pin_get(dt.reset_pin),
		    dt.have_int ? qsoc_pin_get(dt.int_pin) : -1);
	}
	if (r != QUP_OK) {
		unsigned a, n = 0;
		char seen[96];
		int len = 0;

		seen[0] = '\0';
		for (a = 0x08; a < 0x78; a++)
			if (qup_read_reg(&qup, a, 0x00, &v, 1) == QUP_OK) {
				n++;
				if (len < (int)sizeof(seen) - 6)
					len += snprintf(seen + len,
					    sizeof(seen) - len, " 0x%02x", a);
			}
		log_warn(&tlog, "no answer from 0x%02x: %d; on the bus %u "
		    "device(s):%s\n", dt.addr, r, n, n ? seen : " none");
		return r;
	}

	/* Who it is: EDT's "EP0..." id, or FocalTech's chip id at 0xa8. */
	if ((r = reg_read(0xbb, id, sizeof(id))) != QUP_OK) {
		log_warn(&tlog, "no answer from 0x%02x to 0xbb: %d\n", dt.addr,
		    r);
		return r;
	}
	if ((id[1] == 'E' || id[1] == 'e') && (id[2] == 'P' || id[2] == 'p') &&
	    id[3] == '0') {
		log_warn(&tlog, "an EDT M06 controller: a protocol this driver "
		    "does not speak\n");
		return ENODEV;
	}
	if (reg_read(0xa8, &v, 1) == QUP_OK) {
		chip_id = v;
		ev_ft = (v == 0x59);
		log_debug(&tlog, "FocalTech chip id 0x%02x%s\n", v,
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

/* What the keyboard shows, for fb to draw. */
static void
publish(void)
{
	static int failed;
	int r;

	if ((r = ds_publish_u32(OSK_DS_STATE, keys_word(&keys),
	    DSF_OVERWRITE)) != OK && failed++ < 3)
		log_warn(&tlog, "cannot publish the keyboard's state: %d\n", r);
}

/* fb said something: is its console, keyboard and all, on the screen? */
static void
console_check(void)
{
	char key[DS_MAX_KEYLEN];
	endpoint_t owner;
	u32_t v;
	int type, changed = 0;

	while (ds_check(key, &type, &owner) == OK) {
		if (strcmp(key, OSK_DS_CONSOLE) != 0)
			continue;
		if (ds_retrieve_u32(OSK_DS_CONSOLE, &v) != OK)
			v = 0;			/* fb is gone */
		changed |= keys_show(&keys, (v & (OSK_CON_SHOWN |
		    OSK_CON_KEYBOARD)) == (OSK_CON_SHOWN | OSK_CON_KEYBOARD));
		log_debug(&tlog, "console 0x%x: the keyboard takes %s\n", v,
		    keys.shown ? "touches" : "no touches");
	}
	if (changed)
		publish();
}

static void
touch_other(message *m, int ipc_status)
{
	if (is_ipc_notify(ipc_status) && m->m_source == DS_PROC_NR && osk_on)
		console_check();
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
	int r, changed = 0;

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
		/* The first few, where a person at the screen reads them. */
		if (shown < 6) {
			shown++;
			log_debug(&tlog, "touch %u: %s at %u,%u (line was low in "
			    "%lu of %lu polls)\n", tid, ev == 0 ? "down" :
			    ev == 1 ? "up" : "contact", x, y, int_low, polls);
		}
		log_debug(&tlog, "touch %u: %s at %u,%u\n", tid,
		    ev == 0 ? "down" : ev == 1 ? "up" : "contact", x, y);
		if (osk_on)
			changed |= keys_touch(&keys, tid, (int)ev, x, y);
	}
	for (i = 0; i < MAX_TOUCH; i++)
		if ((active & (1u << i)) && !(seen & (1u << i)) && osk_on)
			changed |= keys_lost(&keys, i);
	active = seen;
	if (changed)
		publish();		/* once a frame, however many touches */
}

/*
 * inject=: one step a tick, through keys_touch() at the key's centre - the
 * path a touch takes, but for the controller.
 */
static void
inject_step(void)
{
	const struct osk_layer *l;
	const struct osk_key *k = NULL;
	char item[32];
	const char *name;
	unsigned i, n, x0, y0, x1, y1;
	int hold;

	if (!keys.shown)
		return;			/* keys only where they are drawn */
	if (inject_tap) {
		inject_tap = 0;
		if (keys_touch(&keys, 0, TOUCH_UP, 0, 0))
			publish();
		return;
	}
	if (inject_wait > 0) {
		inject_wait--;
		return;
	}
	if (inject_at == NULL || *inject_at == '\0')
		return;
	n = strcspn(inject_at, ",");
	if (n >= sizeof(item))
		n = sizeof(item) - 1;
	memcpy(item, inject_at, n);
	item[n] = '\0';
	inject_at += strcspn(inject_at, ",");
	if (*inject_at == ',')
		inject_at++;

	if (item[0] == '~') {
		inject_wait = (unsigned)strtoul(item + 1, NULL, 10);
		return;
	}
	hold = item[0] == '+' && item[1] != '\0';
	name = hold ? item + 1 : item;
	if (strcmp(name, "Space") == 0)
		name = "";
	else if (strcmp(name, "Comma") == 0)
		name = ",";
	l = &osk_layers[osk_shown(&keys.osk)];
	for (i = 0; i < l->nkeys && k == NULL; i++)
		if (strcmp(l->keys[i].label, name) == 0)
			k = &l->keys[i];
	if (k == NULL) {
		log_warn(&tlog, "inject: no key \"%s\" on layer %u\n", item,
		    osk_shown(&keys.osk));
		return;
	}
	osk_rect(k, keys.osk.w, keys.osk.h, &x0, &y0, &x1, &y1);
	/* A held key keeps its own touch; a tapped one is touch 0. */
	i = hold ? 1 + inject_held++ % (OSK_MAX_TOUCH - 1) : 0;
	if (keys_touch(&keys, i, TOUCH_DOWN, (x0 + x1) / 2,
	    keys.top + (y0 + y1) / 2))
		publish();
	inject_tap = !hold;
	log_debug(&tlog, "inject: %s \"%s\", word 0x%08x\n", hold ? "hold" :
	    "tap", item, keys_word(&keys));
}

static void
arm(void)
{
	(void)sys_setalarm(sys_hz() / POLL_HZ > 0 ? sys_hz() / POLL_HZ : 1, 0);
}

static void
touch_alarm(clock_t UNUSED(stamp))
{
	uint8_t td;
	int r;

	if (osk_on)
		inject_step();
	if (!hw) {
		arm();
		return;
	}

	/*
	 * Every poll asks the controller how many touches it has (TD_STATUS,
	 * one byte), and reads the frame when it has any, or when a touch
	 * was down at the last look - that frame carries the lift.
	 *
	 * The first version read only while the interrupt line was low.  The
	 * line is not a level: in its default ("trigger") mode the FT5x06
	 * pulses it for each new frame, and a 50 Hz look at a pulse that
	 * short almost never lands on it - on the Mi Pad 4 the keyboard did
	 * not answer a single touch.  The line is still counted, for the log.
	 */
	polls++;
	if (dt.have_int && !qsoc_pin_get(dt.int_pin))
		int_low++;
	td = 0xff;
	if ((r = reg_read(0x02, &td, 1)) != QUP_OK) {
		if (errors++ < 5)
			log_warn(&tlog, "status read failed: %d\n", r);
	} else if ((td & 0x0f) != 0 || active != 0)
		read_frame();

	/*
	 * For the first ninety seconds, every ten: what the polling sees.  On
	 * a machine whose only log is its screen this is how a person there
	 * tells "no touches arrive" from "touches arrive and miss the keys".
	 */
	if (polls % (10 * POLL_HZ) == 0 && polls <= 90 * POLL_HZ)
		log_debug(&tlog, "%lu polls: %lu frames, %lu errors (last %d), "
		    "status 0x%02x, line low %lu times, keyboard %s\n", polls,
		    frames, errors, r, td, int_low, osk_on ? "on" : "off");
	arm();
}

static struct inputdriver touch_tab = {
	.idr_alarm	= touch_alarm,
	.idr_other	= touch_other
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
	if (env_get_param("inject", inject, sizeof(inject)) == OK &&
	    inject[0] != '\0') {
		inject_at = inject;
		osk_on = 1;
	}
	/* No touch screen is the end, unless inject= stands in for one. */
	if ((r = bring_up()) != OK && (inject_at == NULL || dt.found))
		return r;
	hw = (r == OK);
	keys_init(&keys, dt.scr_w ? dt.scr_w : dt.size_x,
	    dt.scr_h ? dt.scr_h : dt.size_y, emit);
	if (keys.w == 0 || keys.h == 0) {
		log_warn(&tlog, "no screen size in the tree\n");
		return EINVAL;
	}
	if (osk_on) {
		/* fb's word, whenever it changes - and now, if it is there. */
		if ((r = ds_subscribe("fb\\.console", DSF_INITIAL |
		    DSF_OVERWRITE)) != OK) {
			log_warn(&tlog, "cannot ask DS about the console: %d\n",
			    r);
			return r;
		}
		publish();		/* a restarted driver holds nothing */
	}
	if (!hw) {
		log_info(&tlog, "no touch screen; inject=%s on a %ux%u "
		    "screen\n", inject, keys.w, keys.h);
		inputdriver_announce(INPUT_DEV_KBD);
		arm();
		return OK;
	}
	/*
	 * One line when it works; the bring-up details, the first touches
	 * and the ten-second summaries are at debug level (log=3), as the
	 * person at the Mi Pad 4 asked once the touch screen worked.
	 */
	log_info(&tlog, "FocalTech 0x%02x at 0x%02x ready; the on-screen "
	    "keyboard %s\n", chip_id, dt.addr, osk_on ? "is live" :
	    "is off (osk=0)");
	inputdriver_announce(INPUT_DEV_KBD);
	arm();
	return OK;
}

static void
touch_signal(int signo)
{
	if (signo != SIGTERM)
		return;
	/* Nothing held down after we are gone, and fb draws it so. */
	if (osk_on && keys_show(&keys, 0))
		publish();
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
