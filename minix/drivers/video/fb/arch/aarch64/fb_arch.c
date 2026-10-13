/*
 * The frame buffer on AArch64: which display this machine has, and the
 * order it is brought up in.
 *
 * Nothing here knows a board.  The parts are found in the device tree by
 * their compatible strings, the way every driver of this port finds its
 * device, and RS grants their registers by the same strings
 * (devicetree "..." lines in fb.conf).  What is here is the order, which is
 * not negotiable and was the vendor kernel's before it was ours:
 *
 *   1. power and clocks, because a block without its bus clock does not
 *      answer at all - the first register read would hang the bus;
 *   2. the encoder, far enough to ask it whether a monitor is plugged in
 *      and what the monitor says it can show (EDID);
 *   3. the pixel clock for the chosen mode;
 *   4. the display controller, reading a frame buffer in RAM;
 *   5. the encoder for that mode, and its PHY last.
 *
 * Then the proof that needs no monitor: the controller's line counter
 * moves, a frame starts every 16.7 ms, the fetch keeps up, the PHY's PLL is
 * locked - and every register written is printed beside what the board's
 * Linux had in it (port/cb2-display/).
 *
 * Without a monitor the mode is 1280x720 at 60 Hz, the one the reference
 * was taken at; a sink must accept it (CEA-861 VIC 4).  With one, its
 * preferred mode, if the pixel clock can be made and the controller's
 * window can reach the frame buffer.
 */

#include <minix/chardriver.h>
#include <minix/drivers.h>
#include <minix/fb.h>
#include <minix/fdt.h>
#include <minix/log.h>
#include <minix/syslib.h>
#include <minix/sysutil.h>
#include <minix/cachectl.h>

#include <sys/ioctl.h>
#include <sys/mman.h>

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dev/videomode/videomode.h>
#include <dev/videomode/edidvar.h>
#include <dev/videomode/edidreg.h>

#include "fb.h"
#include "fbcon.h"
#include "display.h"
#include "fb_ioc.h"

static void ring_log(struct log *driver, int level, const char *file,
	const char *function, int line, const char *fmt, ...);

struct log fb_log = {
	.name = "fb",
	.log_level = LEVEL_INFO,
	.log_func = ring_log
};

/*
 * The parts this file knows, by compatible string.  "rk3568-vop" is what
 * the vendor tree of the board says; mainline Linux calls the RK3566's
 * controller "rk3566-vop".  The encoder string is the Rockchip glue's, which
 * is what decides fb_rk.c - a DesignWare HDMI on another SoC would need its
 * own glue file, not this one.
 */
static const char *const vop_compatibles[] = {
	"rockchip,rk3568-vop",
	"rockchip,rk3566-vop",
};
static const char *const hdmi_compatibles[] = {
	"rockchip,rk3568-dw-hdmi",
};

/* 1280x720 at 60 Hz, CEA-861 VIC 4: the reference mode. */
static const struct videomode mode_720p = {
	.dot_clock = 74250,
	.hdisplay = 1280, .hsync_start = 1390, .hsync_end = 1430,
	.htotal = 1650,
	.vdisplay = 720, .vsync_start = 725, .vsync_end = 730, .vtotal = 750,
	.flags = VID_PHSYNC | VID_PVSYNC,
	.name = "1280x720x60",
};

/*
 * The largest pixel clock this driver will choose by itself: 1920x1080 at
 * 60 Hz.  The parts go further (the controller to 4K, the PHY to 340 MHz
 * without scrambling), but nothing above this has been seen working on the
 * board, and a monitor's preferred mode above it is often 4K at 30 Hz,
 * which is a worse first picture than 1080p.
 */
#define MAX_AUTO_KHZ	148500

#define BYTES_PER_PIXEL	4

/* The most one cache maintenance request covers (see arch_fb_flush()). */
#define FLUSH_PIECE	(128 * 1024)

static struct disp_info disp;
static struct videomode mode;
static int hdmi_mode;			/* 0: DVI */
static int status;			/* 0 not yet, 1 up, -1 failed */
static vir_bytes fb_vir;
static phys_bytes fb_phys;
static size_t fb_size;
static unsigned fb_stride;
static int have_edid;
static struct edid_info edid;

/*
 * The scanout buffers a compositor draws into (FBIOC_MAP, FBIOC_FLIP).
 * Buffer 0 is fb_vir, the console's; the others are made on the first
 * FBIOC_MAP and kept for the life of the driver - a process maps them
 * through VM's shared memory, which looks for the driver's region on every
 * page it touches, so a buffer must not go while anybody may map it.
 */
static vir_bytes buf_vir[FB_MAX_BUFFERS];
static phys_bytes buf_phys[FB_MAX_BUFFERS];
static int nbuf;			/* made so far */
static int shown;			/* the one the controller reads */
static endpoint_t owner = NONE;		/* who has mapped them */

/*
 * virtual=1: no display at all, the buffers only - for QEMU, which has no
 * controller this driver knows.  Everything above the hardware works as on
 * the board (mapping, flipping, the console, reading the screen back), and
 * that is what it is for: the compositor's output path tested without one.
 */
static int virtual_mode;

/* The journal: everything logged, printed and kept (see fb_ioc.h). */
#define RING_BYTES	16384
static char ring[RING_BYTES];
static uint64_t ring_total;

static void
ring_log(struct log *driver, int level, const char *UNUSED(file),
	const char *function, int line, const char *fmt, ...)
{
	char buf[320];
	va_list args;
	const char *s;
	int n;

	if (level > driver->log_level)
		return;
	if (driver->log_level >= LEVEL_DEBUG)
		n = snprintf(buf, sizeof(buf), "%s(%s)%s+%d: ", driver->name,
		    level_string[level], function, line);
	else
		n = snprintf(buf, sizeof(buf), "%s(%s)", driver->name,
		    level_string[level]);
	if (n < 0 || (size_t)n >= sizeof(buf))
		n = 0;
	va_start(args, fmt);
	(void)vsnprintf(buf + n, sizeof(buf) - n, fmt, args);
	va_end(args);

	for (s = buf; *s != '\0'; s++)
		ring[ring_total++ % RING_BYTES] = *s;
	printf("%s", buf);
}

/* The tree. */
struct search {
	struct disp_info *d;
	int vop_found, hdmi_found;
	int vop_disabled, hdmi_disabled;
};

static int
is_one_of(const struct fdt_node *node, const char *const *list, unsigned n,
	const char **which)
{
	unsigned i;

	for (i = 0; i < n; i++)
		if (fdt_node_is_compatible(node, list[i])) {
			if (which != NULL)
				*which = list[i];
			return 1;
		}
	return 0;
}

static int
node_enabled(const struct fdt_node *node)
{
	const char *s;
	unsigned len;

	if ((s = fdt_getprop(node, "status", &len)) == NULL || len == 0)
		return 1;
	return strcmp(s, "okay") == 0 || strcmp(s, "ok") == 0;
}

static u32_t
prop_u32(const struct fdt_node *node, const char *name, unsigned index)
{
	const void *p;
	unsigned len;

	if ((p = fdt_getprop(node, name, &len)) == NULL || len < 4 * (index + 1))
		return 0;
	return (u32_t)fdt_read_cells((const char *)p + 4 * index, 1);
}

static void
take_reg(const struct fdt_node *node, struct disp_regs *r)
{
	u64_t base, size;

	if (fdt_node_reg(node, 0, &base, &size) == 0) {
		r->base = (phys_bytes)base;
		r->size = (size_t)size;
	}
}

static int
find_node(void *cookie, int UNUSED(depth), const char *UNUSED(name),
	const struct fdt_node *node)
{
	struct search *s = cookie;
	struct disp_info *d = s->d;
	const void *p;
	unsigned len, i;
	u64_t base, size;

	if (!s->vop_found && is_one_of(node, vop_compatibles,
	    sizeof(vop_compatibles) / sizeof(vop_compatibles[0]),
	    &d->vop_compatible)) {
		if (!node_enabled(node)) {
			s->vop_disabled = 1;
			return 0;
		}
		s->vop_found = 1;
		take_reg(node, &d->vop);
		/* "power-domains" is <controller domain>. */
		d->power_domain = fdt_getprop(node, "power-domains", &len) &&
		    len >= 8 ? (int)prop_u32(node, "power-domains", 1) : -1;
		if (d->grf.base == 0 && fdt_phandle_reg(node->dtb,
		    prop_u32(node, "rockchip,grf", 0), 0, &base, &size) == 0) {
			d->grf.base = (phys_bytes)base;
			d->grf.size = (size_t)size;
		}
		return 0;
	}

	if (!s->hdmi_found && is_one_of(node, hdmi_compatibles,
	    sizeof(hdmi_compatibles) / sizeof(hdmi_compatibles[0]), NULL)) {
		if (!node_enabled(node)) {
			s->hdmi_disabled = 1;
			return 0;
		}
		s->hdmi_found = 1;
		take_reg(node, &d->hdmi);
		if ((p = fdt_getprop(node, "pinctrl-0", &len)) != NULL)
			for (i = 0; i + 4 <= len && d->npinctrl <
			    sizeof(d->pinctrl) / sizeof(d->pinctrl[0]); i += 4)
				d->pinctrl[d->npinctrl++] = (uint32_t)
				    fdt_read_cells((const char *)p + i, 1);
		return 0;
	}

	if (d->cru.base == 0 && (fdt_node_is_compatible(node,
	    "rockchip,rk3568-cru") || fdt_node_is_compatible(node,
	    "rockchip,rk3566-cru")))
		take_reg(node, &d->cru);
	else if (d->pmucru.base == 0 && (fdt_node_is_compatible(node,
	    "rockchip,rk3568-pmucru") || fdt_node_is_compatible(node,
	    "rockchip,rk3566-pmucru")))
		take_reg(node, &d->pmucru);
	else if (d->pmu.base == 0 && (fdt_node_is_compatible(node,
	    "rockchip,rk3568-pmu") || fdt_node_is_compatible(node,
	    "rockchip,rk3566-pmu")))
		take_reg(node, &d->pmu);
	return 0;
}

static int
find_display(const void *dtb)
{
	struct search s;

	memset(&disp, 0, sizeof(disp));
	disp.power_domain = -1;
	memset(&s, 0, sizeof(s));
	s.d = &disp;
	fdt_walk(dtb, find_node, &s);

	if (!s.vop_found || !s.hdmi_found) {
		if (s.vop_disabled || s.hdmi_disabled)
			log_warn(&fb_log, "the display is in the tree but "
			    "disabled there (status); on the CB2 the Armbian "
			    "overlay \"hdmi\" enables it\n");
		else
			log_warn(&fb_log, "no display this driver knows in the "
			    "tree; nothing to do\n");
		return ENXIO;
	}

	log_info(&fb_log, "%s at 0x%lx, hdmi at 0x%lx, grf 0x%lx, cru 0x%lx, "
	    "pmucru 0x%lx, pmu 0x%lx, power domain %d, %u pin group(s)\n",
	    disp.vop_compatible, (unsigned long)disp.vop.base,
	    (unsigned long)disp.hdmi.base, (unsigned long)disp.grf.base,
	    (unsigned long)disp.cru.base, (unsigned long)disp.pmucru.base,
	    (unsigned long)disp.pmu.base, disp.power_domain, disp.npinctrl);
	return OK;
}

static int
map(struct disp_regs *r, const char *what)
{
	void *v;

	if ((v = vm_map_phys(SELF, (void *)r->base, r->size)) == MAP_FAILED) {
		log_warn(&fb_log, "cannot map the %s at 0x%lx; not granted by "
		    "RS?\n", what, (unsigned long)r->base);
		return EPERM;
	}
	r->v = v;
	return OK;
}

/*
 * The monitor, if there is one: hot plug, then EDID over the encoder's DDC.
 * The preferred mode is taken if this driver can make it; otherwise the
 * largest mode the monitor lists that it can; otherwise 720p.
 */
static int
mode_usable(const struct videomode *m)
{
	return m->dot_clock > 0 && m->dot_clock <= MAX_AUTO_KHZ &&
	    !(m->flags & (VID_INTERLACE | VID_DBLSCAN)) &&
	    m->hdisplay <= 1920 && m->vdisplay <= 1080;
}

static void
choose_mode(void)
{
	uint8_t block[128];
	int i, best;
	const char *why;

	mode = mode_720p;
	hdmi_mode = 0;

	if (!dwhdmi_hpd()) {
		log_info(&fb_log, "no monitor on the connector (hot plug low); "
		    "%s, as in the reference\n", mode.name);
		return;
	}
	if (dwhdmi_read_edid(0, block) != OK || edid_is_valid(block) != 0 ||
	    edid_parse(block, &edid) != 0) {
		log_warn(&fb_log, "a monitor is plugged in, but its EDID did "
		    "not read; %s\n", mode.name);
		return;
	}
	have_edid = 1;
	log_info(&fb_log, "monitor: %s %s (%s), EDID %d.%d, %d mode(s)\n",
	    edid.edid_vendorname, edid.edid_productname, edid.edid_serial,
	    edid.edid_version, edid.edid_revision, edid.edid_nmodes);

	if (edid.edid_preferred_mode != NULL &&
	    mode_usable(edid.edid_preferred_mode)) {
		mode = *edid.edid_preferred_mode;
		why = "its preferred mode";
	} else {
		best = -1;
		for (i = 0; i < edid.edid_nmodes; i++)
			if (mode_usable(&edid.edid_modes[i]) && (best < 0 ||
			    edid.edid_modes[i].hdisplay *
			    edid.edid_modes[i].vdisplay >
			    edid.edid_modes[best].hdisplay *
			    edid.edid_modes[best].vdisplay))
				best = i;
		if (best < 0) {
			log_info(&fb_log, "no mode of the monitor's is one "
			    "this driver chooses; %s\n", mode.name);
			return;
		}
		mode = edid.edid_modes[best];
		why = "the largest it lists that this driver makes";
	}
	if (mode.name == NULL)
		mode.name = "(edid)";
	log_info(&fb_log, "mode %dx%d, %d kHz: %s\n", mode.hdisplay,
	    mode.vdisplay, mode.dot_clock, why);
}

static int
alloc_frame_buffer(void)
{
	void *v;

	fb_stride = mode.hdisplay * BYTES_PER_PIXEL;
	fb_size = (size_t)fb_stride * mode.vdisplay;

	/*
	 * Physically contiguous - the window fetches it as one range - and
	 * below 4 GiB, which the window's address register needs and every
	 * byte of RAM on this SoC is.  The memory is ordinary cacheable RAM;
	 * arch_fb_flush() cleans what was drawn, because the controller
	 * reads behind the caches.
	 */
	if ((v = alloc_contig(fb_size, AC_ALIGN64K, &fb_phys)) == NULL) {
		log_warn(&fb_log, "cannot allocate %zu contiguous bytes for "
		    "the frame buffer\n", fb_size);
		return ENOMEM;
	}
	fb_vir = (vir_bytes)v;
	buf_vir[0] = fb_vir;		/* before the first flush: it uses these */
	buf_phys[0] = fb_phys;
	nbuf = 1;
	memset(v, 0, fb_size);
	{
		/* What a whole-frame clean costs: the console pays it per scroll. */
		u64_t t0, t1;

		read_frclock_64(&t0);
		arch_fb_flush(0, 0, fb_size);
		read_frclock_64(&t1);
		log_info(&fb_log, "cleaning the whole frame out of the cache: "
		    "%u us\n", frclock_64_to_micros(delta_frclock_64(t0, t1)));
	}
	log_info(&fb_log, "frame buffer %ux%u, stride %u, %zu bytes at "
	    "physical 0x%lx\n", mode.hdisplay, mode.vdisplay, fb_stride,
	    fb_size, (unsigned long)fb_phys);
	return OK;
}

/* Clean rows y0..y1-1 of a buffer out of the cache, in pieces. */
static void
flush_rows(int b, unsigned y0, unsigned y1)
{
	size_t off, len, n;

	if (y1 > (unsigned)mode.vdisplay)
		y1 = mode.vdisplay;
	if (y0 >= y1)
		return;
	off = (size_t)y0 * fb_stride;
	len = (size_t)(y1 - y0) * fb_stride;
	while (len > 0) {
		n = len < FLUSH_PIECE ? len : FLUSH_PIECE;
		(void)sys_cachectl(CACHE_CLEAN, (void *)(buf_vir[b] + off), n);
		off += n;
		len -= n;
	}
}

/* Put buffer b on the screen; the call returns when it is there. */
static int
show(int b)
{
	int r = OK;

	if (!virtual_mode && (r = vop2_scanout(buf_phys[b])) != OK)
		log_warn(&fb_log, "buffer %d did not reach the screen: %d\n",
		    b, r);
	if (r == OK)
		shown = b;
	return r;
}

/*
 * FBIOC_MAP: every scanout buffer, mapped into the asker, who becomes the
 * owner of the screen.  The second and third are made now, the first time,
 * and kept.
 */
static int
do_map(endpoint_t ep, cp_grant_id_t gid, endpoint_t user_ep)
{
	struct fb_map m;
	void *v;
	int i;

	if (owner != NONE && owner != user_ep)
		return EBUSY;

	while (nbuf < FB_MAX_BUFFERS) {
		if ((v = alloc_contig(fb_size, AC_ALIGN64K,
		    &buf_phys[nbuf])) == NULL) {
			log_warn(&fb_log, "no memory for scanout buffer %d\n",
			    nbuf);
			break;
		}
		memset(v, 0, fb_size);
		buf_vir[nbuf] = (vir_bytes)v;
		flush_rows(nbuf, 0, mode.vdisplay);
		nbuf++;
	}

	memset(&m, 0, sizeof(m));
	m.width = mode.hdisplay;
	m.height = mode.vdisplay;
	m.stride = fb_stride;
	m.format = FB_FORMAT_XRGB8888;
	m.size = (uint32_t)fb_size;
	m.nbuf = nbuf;
	m.shown = shown;
	for (i = 0; i < nbuf; i++) {
		v = vm_remap(user_ep, sef_self(), NULL, (void *)buf_vir[i],
		    fb_size);
		if (v == MAP_FAILED) {
			log_warn(&fb_log, "cannot map buffer %d into %d\n", i,
			    user_ep);
			return ENOMEM;
		}
		m.addr[i] = (uint64_t)(uintptr_t)v;
	}
	owner = user_ep;
	log_info(&fb_log, "%d scanout buffer(s) mapped into %d\n", nbuf,
	    user_ep);
	return sys_safecopyto(ep, gid, 0, (vir_bytes)&m, sizeof(m));
}

/* FBIOC_FLIP: clean what was drawn, then show it. */
static int
do_flip(endpoint_t ep, cp_grant_id_t gid, endpoint_t user_ep)
{
	struct fb_flip f;
	int r;

	if (user_ep != owner)
		return EPERM;
	if ((r = sys_safecopyfrom(ep, gid, 0, (vir_bytes)&f, sizeof(f))) != OK)
		return r;
	if (f.buf >= (uint32_t)nbuf)
		return EINVAL;
	if (f.y1 > f.y0)
		flush_rows((int)f.buf, f.y0, f.y1);
	else
		flush_rows((int)f.buf, 0, mode.vdisplay);
	return show((int)f.buf);
}

void
arch_fb_release(int minor)
{
	if (minor != 0 || status <= 0)
		return;
	if (owner != NONE)
		log_info(&fb_log, "the screen is given back by %d\n", owner);
	owner = NONE;
	if (shown != 0)
		(void)show(0);
}

static void
report_state(void)
{
	unsigned line;
	char stats[256];

	if (virtual_mode) {
		log_info(&fb_log, "state: virtual, mode %dx%d, buffer %d of %d "
		    "shown, owner %d\n", mode.hdisplay, mode.vdisplay, shown,
		    nbuf, owner);
		fbcon_stats(stats, sizeof(stats));
		log_info(&fb_log, "%s\n", stats);
		return;
	}
	log_info(&fb_log, "state: monitor %s, PHY %s, mode %dx%d, buffer %d of "
	    "%d shown, owner %d\n",
	    dwhdmi_hpd() ? "plugged in" : "absent",
	    dwhdmi_phy_locked() ? "locked" : "NOT locked",
	    mode.hdisplay, mode.vdisplay, shown, nbuf, owner);
	(void)vop2_scanning(&line);
	fbcon_stats(stats, sizeof(stats));
	log_info(&fb_log, "%s\n", stats);
}

static int
bring_up(void)
{
	void *dtb;
	unsigned got;
	long v, report = 1, force = -1, hdmi = 0;
	int r;

	/* Arguments, all optional: env_parse() writes a long, always. */
	(void)env_parse("report", "d", 0, &report, 0, 1);
	if (env_parse("log", "d", 0, &v, LEVEL_NONE, LEVEL_TRACE) == EP_SET)
		fb_log.log_level = (int)v;

	if (env_parse("virtual", "d", 0, &v, 0, 1) == EP_SET && v == 1) {
		/* No hardware: a 720p frame in memory, and nothing else. */
		virtual_mode = 1;
		mode = mode_720p;
		if ((r = alloc_frame_buffer()) != OK)
			return r;
		log_info(&fb_log, "virtual display, %dx%d, no controller\n",
		    mode.hdisplay, mode.vdisplay);
		return OK;
	}

	if ((dtb = fdt_fetch()) == NULL) {
		log_warn(&fb_log, "the kernel has no device tree\n");
		return ENXIO;
	}
	r = find_display(dtb);
	if (r == OK && ((r = map(&disp.vop, "display controller")) != OK ||
	    (r = map(&disp.hdmi, "HDMI encoder")) != OK ||
	    (r = rk_vo_map(&disp)) != OK))
		;
	if (r != OK) {
		free(dtb);
		return r;
	}

	/* 1. Power and clocks; the pixel clock at the reference rate. */
	if ((r = rk_vo_power(&disp)) != OK ||
	    (r = rk_vo_clocks(&disp, mode_720p.dot_clock, &got)) != OK) {
		free(dtb);
		return r;
	}
	rk_vo_pins(&disp, dtb);
	free(dtb);

	/* 2. The encoder, far enough to ask about the monitor. */
	dwhdmi_init(&disp.hdmi);
	vop2_init(&disp.vop);
	choose_mode();

	/* 3. The pixel clock for the mode chosen. */
	if (mode.dot_clock != mode_720p.dot_clock &&
	    rk_vo_clocks(&disp, mode.dot_clock, &got) != OK) {
		log_warn(&fb_log, "falling back to %s\n", mode_720p.name);
		mode = mode_720p;
		if ((r = rk_vo_clocks(&disp, mode.dot_clock, &got)) != OK)
			return r;
	}
	mode.dot_clock = got;

	/* 4. The display controller, scanning a black frame buffer. */
	if ((r = alloc_frame_buffer()) != OK)
		return r;
	if ((r = vop2_set_mode(&mode, fb_phys, fb_stride)) != OK)
		return r;

	/* 5. The encoder for the mode, PHY last. */
	(void)env_parse("hdmi", "d", 0, &hdmi, 0, 1);
	hdmi_mode = (int)hdmi;
	if ((r = dwhdmi_set_mode(&mode, hdmi_mode)) != OK)
		return r;

	/*
	 * force=0xRRGGBB: one colour from the encoder instead of the picture.
	 * It tests everything after the display controller without it - if
	 * the colour shows and the picture does not, the fault is upstream.
	 */
	if (env_parse("force", "x", 0, &force, 0, 0xffffff) == EP_SET) {
		dwhdmi_force_color(1, force >> 16, (force >> 8) & 0xff,
		    force & 0xff);
		log_info(&fb_log, "encoder forced to colour 0x%06lx\n", force);
	}

	report_state();
	if (report) {
		rk_vo_report(&disp);
		vop2_report();
		dwhdmi_report();
	}
	return OK;
}

int
arch_fb_init(int minor, struct edid_info *UNUSED(info))
{
	/*
	 * The EDID argument is the earm way, where another driver read it.
	 * Here the encoder reads it itself, so a provider named in the
	 * arguments is not consulted.
	 */
	if (minor != 0)
		return ENXIO;
	if (status == 0)
		status = (bring_up() == OK) ? 1 : -1;
	return status > 0 ? OK : ENXIO;
}

int
arch_get_device(int minor, struct device *dev)
{
	if (minor != 0 || status <= 0)
		return ENXIO;
	/* read(2) shows what is on the screen: fbpeek reads it this way. */
	dev->dv_base = buf_vir[shown];
	dev->dv_size = fb_size;
	return OK;
}

int
arch_get_varscreeninfo(int minor, struct fb_var_screeninfo *v)
{
	if (minor != 0 || status <= 0)
		return ENXIO;
	memset(v, 0, sizeof(*v));
	v->xres = v->xres_virtual = mode.hdisplay;
	v->yres = v->yres_virtual = mode.vdisplay;
	v->bits_per_pixel = BYTES_PER_PIXEL * 8;
	/* XRGB8888, little-endian: the word is 0x00RRGGBB. */
	v->red.offset = 16;
	v->red.length = 8;
	v->green.offset = 8;
	v->green.length = 8;
	v->blue.offset = 0;
	v->blue.length = 8;
	v->transp.offset = 24;
	v->transp.length = 0;
	return OK;
}

int
arch_put_varscreeninfo(int minor, struct fb_var_screeninfo *v)
{
	/* The mode is chosen at bring-up; asking for the same one is fine. */
	if (minor != 0 || status <= 0)
		return ENXIO;
	if (v->xres != (u32_t)mode.hdisplay || v->yres != (u32_t)mode.vdisplay ||
	    v->bits_per_pixel != BYTES_PER_PIXEL * 8 || v->yoffset != 0)
		return EINVAL;
	return OK;
}

int
arch_get_fixscreeninfo(int minor, struct fb_fix_screeninfo *f)
{
	if (minor != 0 || status <= 0)
		return ENXIO;
	memset(f, 0, sizeof(*f));
	strlcpy(f->id, "rk-vop2-hdmi", sizeof(f->id));
	f->line_length = fb_stride;
	return OK;
}

int
arch_pan_display(int minor, struct fb_var_screeninfo *v)
{
	if (minor != 0 || status <= 0)
		return ENXIO;
	return (v->xoffset == 0 && v->yoffset == 0) ? OK : EINVAL;
}

void
arch_fb_flush(int minor, size_t off, size_t len)
{
	if (minor != 0 || fb_vir == 0 || off >= fb_size)
		return;
	if (len > fb_size - off)
		len = fb_size - off;

	/*
	 * In pieces.  The kernel does the whole of one request under its big
	 * lock, and every other core that enters the kernel meanwhile waits.
	 * A whole 720p frame - 900 pages, 57600 cache lines - was measured at
	 * about a millisecond on the board, which is tolerable; a 4K frame is
	 * nine times that, and the console cleans the whole frame on every
	 * scroll, up to ten times a second.  The total work is the same; no
	 * single request holds the lock for more than a 128 KiB piece of it.
	 */
	/* The buffer on the screen: the console's, unless a program owns it. */
	while (len > 0) {
		size_t n = len < FLUSH_PIECE ? len : FLUSH_PIECE;

		(void)sys_cachectl(CACHE_CLEAN, (void *)(buf_vir[shown] + off),
		    n);
		off += n;
		len -= n;
	}
}

int
arch_fb_ioctl(int minor, unsigned long request, endpoint_t ep,
	cp_grant_id_t gid, endpoint_t user_ep)
{
	static struct fb_logdump d;
	uint64_t from, i;

	if (minor != 0)
		return ENXIO;
	if (request == FBIOC_MAP || request == FBIOC_FLIP) {
		if (status <= 0)
			return ENXIO;
		return request == FBIOC_MAP ? do_map(ep, gid, user_ep) :
		    do_flip(ep, gid, user_ep);
	}
	if (request != FBIOC_LOG)
		return ENOTTY;

	if (status > 0)
		report_state();
	d.total = ring_total;
	d.len = ring_total < sizeof(d.text) ? (uint32_t)ring_total :
	    (uint32_t)sizeof(d.text);
	from = ring_total - d.len;
	for (i = 0; i < d.len; i++)
		d.text[i] = ring[(from + i) % RING_BYTES];
	return sys_safecopyto(ep, gid, 0, (vir_bytes)&d, sizeof(d));
}
