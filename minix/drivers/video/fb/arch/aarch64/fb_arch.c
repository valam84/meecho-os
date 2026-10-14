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
 * window can reach the frame buffer; else the largest of its list that can
 * be, the CEA-861 extension's modes included.  fbmode=WIDTHxHEIGHT[@HZ]
 * (service argument or boot parameter) picks one of that list instead, and
 * fblog's report shows the list.  FBIOC_SETMODE changes the mode while the
 * driver runs - a compositor asks, the console follows - in the same order
 * as bring-up, under scanout buffers made for the largest mode of the list.
 */

#include <minix/chardriver.h>
#include <minix/ds.h>
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
#include "meecho_drm.h"

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
#define MAX_W		1920		/* and the largest frame */
#define MAX_H		1080

#define BYTES_PER_PIXEL	4

/* The most one cache maintenance request covers (see arch_fb_flush()). */
#define FLUSH_PIECE	(128 * 1024)

static struct disp_info disp;
static struct videomode mode;
static int hdmi_mode;			/* 0: DVI */
static int status;			/* 0 not yet, 1 up, -1 failed */
static vir_bytes fb_vir;
static phys_bytes fb_phys;
static size_t fb_size;			/* the frame of the mode: stride x lines */
static unsigned fb_stride;
static size_t buf_bytes;		/* each buffer: the largest frame */
static int have_edid;
static struct edid_info edid;

/*
 * The mode the driver would choose now (pick_mode()), kept from the last
 * time the monitor was read - FBIOC_MODES says it to a compositor, which
 * polls - and the monitors come since bring-up (fb_ioc.h).
 */
static struct videomode preferred;
static uint32_t generation;

/*
 * The scanout buffers a compositor draws into (FBIOC_MAP, FBIOC_FLIP).
 * Buffer 0 is fb_vir, the console's; the others are made on the first
 * FBIOC_MAP and kept for the life of the driver - a process maps them
 * through VM's shared memory, which looks for the driver's region on every
 * page it touches, so a buffer must not go while anybody may map it.  That
 * is why each is as large as the largest frame this driver makes at all
 * (buf_bytes): a mode change keeps them all, and the mappings with them,
 * whatever monitor comes after bring-up.
 */
static vir_bytes buf_vir[FB_MAX_BUFFERS];
static phys_bytes buf_phys[FB_MAX_BUFFERS];
static int nbuf;			/* made so far */
static int shown;			/* the one the controller reads */
static endpoint_t owner = NONE;		/* who has mapped them */
/*
 * Drawn by the GPU since a processor last had it (FB_FLIP_GPU): what the
 * caches hold of it is older than memory, and is dropped before this
 * driver reads it or the console draws into it (drop_buffer()).
 */
static int gpu_drawn[FB_MAX_BUFFERS];

/*
 * The cursor's pictures (FBIOC_CURSOR_MAP, FBIOC_CURSOR): made on the first
 * map and kept, for the reason the scanout buffers are.  "cur" is what the
 * owner asked for last, kept to be put back after a mode change and to be
 * blended into what read(2) answers.
 */
#define CURSOR_STRIDE	(FB_CURSOR_MAX * BYTES_PER_PIXEL)
#define CURSOR_BYTES	(CURSOR_STRIDE * FB_CURSOR_MAX)
static vir_bytes cur_vir[FB_CURSOR_BUFFERS];
static phys_bytes cur_phys[FB_CURSOR_BUFFERS];
static int ncur;
static struct fb_cursor cur;		/* flags 0: hidden */
static unsigned long cur_moves;

/*
 * virtual=1: no display at all, the buffers only - for QEMU, which has no
 * controller this driver knows.  Everything above the hardware works as on
 * the board (mapping, flipping, the console, reading the screen back), and
 * that is what it is for: the compositor's output path tested without one.
 * Its "monitor" can be taken out and another plugged in (FBIOC_VPLUG), so
 * that the hot plug path is tested too: virtual_plug is the line, and
 * virtual_read the monitor whose list was read last.
 */
static int virtual_mode;
static uint32_t virtual_plug = FB_VPLUG_WIDE, virtual_read = FB_VPLUG_WIDE;

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
	    m->hdisplay <= MAX_W && m->vdisplay <= MAX_H;
}

/*
 * The modes a CEA-861 extension block names by number (VIC, CTA-861-G
 * table 1) that this driver can make, timings from the same table.  A
 * television lists its 720p and 1080p there and only there: the base block
 * has room for four detailed timings, and a 4K set spends them on 4K - the
 * one on this board's bench ("TV-Station") offers 640x480, 800x600,
 * 1024x768, 2160p60 and 1440p120 in the base block, nothing in between.
 */
#define NELEM(a)	(sizeof(a) / sizeof((a)[0]))

static const struct {
	uint8_t vic;
	struct videomode m;
} cea_modes[] = {
#define PHV	(VID_PHSYNC | VID_PVSYNC)
#define CEA(v, clk, h, hs, he, ht, vd, vs, ve, vt, fl, nm) \
	{ v, { .dot_clock = clk, .hdisplay = h, .hsync_start = hs, \
	    .hsync_end = he, .htotal = ht, .vdisplay = vd, .vsync_start = vs, \
	    .vsync_end = ve, .vtotal = vt, .flags = fl, .name = nm } }
	CEA(1, 25175, 640, 656, 752, 800, 480, 490, 492, 525,
	    VID_NHSYNC | VID_NVSYNC, "640x480x60"),
	CEA(2, 27000, 720, 736, 798, 858, 480, 489, 495, 525,
	    VID_NHSYNC | VID_NVSYNC, "720x480x60"),
	CEA(3, 27000, 720, 736, 798, 858, 480, 489, 495, 525,
	    VID_NHSYNC | VID_NVSYNC, "720x480x60"),
	CEA(4, 74250, 1280, 1390, 1430, 1650, 720, 725, 730, 750, PHV,
	    "1280x720x60"),
	CEA(16, 148500, 1920, 2008, 2052, 2200, 1080, 1084, 1089, 1125, PHV,
	    "1920x1080x60"),
	CEA(17, 27000, 720, 732, 796, 864, 576, 581, 586, 625,
	    VID_NHSYNC | VID_NVSYNC, "720x576x50"),
	CEA(18, 27000, 720, 732, 796, 864, 576, 581, 586, 625,
	    VID_NHSYNC | VID_NVSYNC, "720x576x50"),
	CEA(19, 74250, 1280, 1720, 1760, 1980, 720, 725, 730, 750, PHV,
	    "1280x720x50"),
	CEA(31, 148500, 1920, 2448, 2492, 2640, 1080, 1084, 1089, 1125, PHV,
	    "1920x1080x50"),
	CEA(32, 74250, 1920, 2558, 2602, 2750, 1080, 1084, 1089, 1125, PHV,
	    "1920x1080x24"),
	CEA(33, 74250, 1920, 2448, 2492, 2640, 1080, 1084, 1089, 1125, PHV,
	    "1920x1080x25"),
	CEA(34, 74250, 1920, 2008, 2052, 2200, 1080, 1084, 1089, 1125, PHV,
	    "1920x1080x30"),
#undef CEA
#undef PHV
};

/* Append a mode to the monitor's list, unless it is there already. */
static void
add_mode(const struct videomode *m)
{
	int j;

	for (j = 0; j < edid.edid_nmodes; j++)
		if (edid.edid_modes[j].hdisplay == m->hdisplay &&
		    edid.edid_modes[j].vdisplay == m->vdisplay &&
		    edid.edid_modes[j].htotal == m->htotal &&
		    edid.edid_modes[j].vtotal == m->vtotal &&
		    edid.edid_modes[j].dot_clock == m->dot_clock)
			return;
	if (edid.edid_nmodes < (int)NELEM(edid.edid_modes))
		edid.edid_modes[edid.edid_nmodes++] = *m;
}

/*
 * An 18-byte detailed timing descriptor (EDID 1.3, 3.10.2), as the base
 * block's are read by edid_parse(): 0 if it is a timing, -1 if it is a
 * display descriptor (pixel clock 0) or interlaced.
 */
static int
dtd_mode(const uint8_t *p, struct videomode *m)
{
	unsigned ha, hb, va, vb, hso, hsw, vso, vsw;

	if ((p[0] | p[1] << 8) == 0 || (p[17] & 0x80))
		return -1;
	ha = p[2] | (p[4] & 0xf0) << 4;
	hb = p[3] | (p[4] & 0x0f) << 8;
	va = p[5] | (p[7] & 0xf0) << 4;
	vb = p[6] | (p[7] & 0x0f) << 8;
	hso = p[8] | (p[11] & 0xc0) << 2;
	hsw = p[9] | (p[11] & 0x30) << 4;
	vso = (p[10] >> 4) | (p[11] & 0x0c) << 2;
	vsw = (p[10] & 0x0f) | (p[11] & 0x03) << 4;
	memset(m, 0, sizeof(*m));
	m->dot_clock = (p[0] | p[1] << 8) * 10;
	m->hdisplay = (int)ha;
	m->hsync_start = (int)(ha + hso);
	m->hsync_end = (int)(ha + hso + hsw);
	m->htotal = (int)(ha + hb);
	m->vdisplay = (int)va;
	m->vsync_start = (int)(va + vso);
	m->vsync_end = (int)(va + vso + vsw);
	m->vtotal = (int)(va + vb);
	if ((p[17] & 0x18) == 0x18) {		/* digital separate sync */
		m->flags |= (p[17] & 0x04) ? VID_PVSYNC : VID_NVSYNC;
		m->flags |= (p[17] & 0x02) ? VID_PHSYNC : VID_NHSYNC;
	}
	return 0;
}

/*
 * The first extension block, if it is CEA-861: the modes its Video Data
 * Blocks name and cea_modes[] knows, and its own detailed timings - a set
 * keeps its 1080p60 there as often as among the numbers - added to the
 * base block's list.  A monitor without one, or one this cannot read,
 * keeps the base list.
 */
static void
add_cea_modes(void)
{
	uint8_t b[128];
	unsigned d, i, k, len, sum, v, n0 = (unsigned)edid.edid_nmodes;
	struct videomode m;
	int t;

	if (edid.edid_ext_block_count < 1 || dwhdmi_read_edid(1, b) != OK)
		return;
	for (sum = 0, i = 0; i < sizeof(b); i++)
		sum += b[i];
	if (b[0] != 0x02 || (sum & 0xff) != 0) {
		log_info(&fb_log, "extension block: tag 0x%02x, sum 0x%02x; "
		    "not CEA-861, not used\n", b[0], sum & 0xff);
		return;
	}
	d = b[2] >= 4 && b[2] <= 127 ? b[2] : 4;
	for (i = 4; i < d; i += 1 + len) {
		len = b[i] & 0x1f;
		if (i + 1 + len > d)
			break;
		if ((b[i] >> 5) != 2)		/* not a Video Data Block */
			continue;
		for (k = 1; k <= len; k++) {
			v = b[i + k];
			if (v >= 129 && v <= 192)	/* "native" flag */
				v &= 0x7f;
			for (t = 0; t < (int)NELEM(cea_modes); t++)
				if (cea_modes[t].vic == v)
					break;
			if (t < (int)NELEM(cea_modes))
				add_mode(&cea_modes[t].m);
		}
	}
	/* The detailed timings from offset d to the checksum (b[2] 0: none). */
	for (i = b[2] >= 4 && b[2] <= 127 ? d : 128; i + 18 <= 127; i += 18)
		if (dtd_mode(&b[i], &m) == 0)
			add_mode(&m);
	log_info(&fb_log, "CEA-861 extension: %u mode(s) more\n",
	    (unsigned)edid.edid_nmodes - n0);
}

/* A mode's refresh rate in Hz, rounded. */
static unsigned
mode_hz(const struct videomode *m)
{
	unsigned long t = (unsigned long)m->htotal * m->vtotal;

	if (t == 0)
		return 0;
	return (unsigned)(((unsigned long)m->dot_clock * 1000 + t / 2) / t);
}

/*
 * The modes of the virtual display (virtual=1, QEMU): no monitor to ask, so
 * a fixed few, for the mode to be changed there too - the strip's stand
 * changes it while the compositor runs.  The timings are the usual ones;
 * nothing scans them.
 */
static const struct videomode virtual_modes[] = {
	{ .dot_clock = 74250, .hdisplay = 1280, .hsync_start = 1390,
	    .hsync_end = 1430, .htotal = 1650, .vdisplay = 720,
	    .vsync_start = 725, .vsync_end = 730, .vtotal = 750,
	    .flags = VID_PHSYNC | VID_PVSYNC, .name = "1280x720x60" },
	{ .dot_clock = 148500, .hdisplay = 1920, .hsync_start = 2008,
	    .hsync_end = 2052, .htotal = 2200, .vdisplay = 1080,
	    .vsync_start = 1084, .vsync_end = 1089, .vtotal = 1125,
	    .flags = VID_PHSYNC | VID_PVSYNC, .name = "1920x1080x60" },
	{ .dot_clock = 65000, .hdisplay = 1024, .hsync_start = 1048,
	    .hsync_end = 1184, .htotal = 1344, .vdisplay = 768,
	    .vsync_start = 771, .vsync_end = 777, .vtotal = 806,
	    .flags = VID_NHSYNC | VID_NVSYNC, .name = "1024x768x60" },
	{ .dot_clock = 40000, .hdisplay = 800, .hsync_start = 840,
	    .hsync_end = 968, .htotal = 1056, .vdisplay = 600,
	    .vsync_start = 601, .vsync_end = 605, .vtotal = 628,
	    .flags = VID_PHSYNC | VID_PVSYNC, .name = "800x600x60" },
};

/* The other "monitor" of the virtual display (FB_VPLUG_SMALL): two modes. */
static const struct videomode virtual_small[] = {
	{ .dot_clock = 65000, .hdisplay = 1024, .hsync_start = 1048,
	    .hsync_end = 1184, .htotal = 1344, .vdisplay = 768,
	    .vsync_start = 771, .vsync_end = 777, .vtotal = 806,
	    .flags = VID_NHSYNC | VID_NVSYNC, .name = "1024x768x60" },
	{ .dot_clock = 40000, .hdisplay = 800, .hsync_start = 840,
	    .hsync_end = 968, .htotal = 1056, .vdisplay = 600,
	    .vsync_start = 601, .vsync_end = 605, .vtotal = 628,
	    .flags = VID_PHSYNC | VID_PVSYNC, .name = "800x600x60" },
};

/*
 * The modes there are to choose from: the virtual display's, the monitor's
 * (base block and CEA-861 extension), or - no monitor, or one whose EDID
 * did not read - the reference mode alone.
 */
static const struct videomode *
mode_list(int *n)
{
	if (virtual_mode) {
		if (virtual_read == FB_VPLUG_SMALL) {
			*n = (int)NELEM(virtual_small);
			return virtual_small;
		}
		*n = (int)NELEM(virtual_modes);
		return virtual_modes;
	}
	if (have_edid) {
		*n = edid.edid_nmodes;
		return edid.edid_modes;
	}
	*n = 1;
	return &mode_720p;
}

/* The same mode: size, totals and rate (the clock made may be a hair off). */
static int
same_mode(const struct videomode *a, const struct videomode *b)
{
	return a->hdisplay == b->hdisplay && a->vdisplay == b->vdisplay &&
	    a->htotal == b->htotal && a->vtotal == b->vtotal &&
	    mode_hz(a) == mode_hz(b);
}

/* A mode of the list of that size, the rate nearest to hz; NULL if none. */
static const struct videomode *
find_mode(unsigned w, unsigned h, unsigned hz)
{
	const struct videomode *list, *best = NULL;
	unsigned d, dbest = 0;
	int i, n;

	list = mode_list(&n);
	for (i = 0; i < n; i++) {
		if (!mode_usable(&list[i]) || list[i].hdisplay != (int)w ||
		    list[i].vdisplay != (int)h)
			continue;
		d = mode_hz(&list[i]);
		d = d > hz ? d - hz : hz - d;
		if (best == NULL || d < dbest) {
			best = &list[i];
			dbest = d;
		}
	}
	return best;
}

/*
 * The bytes of one scanout buffer: the largest frame this driver makes at
 * all (mode_usable()), so that a mode change never needs other buffers - a
 * compositor has them mapped, and a buffer must not go while it may - not
 * even for a monitor plugged in after bring-up, larger than the list then.
 * 1080p is 8 MB; three of them.
 */
static size_t
buffer_bytes(void)
{
	size_t max = (size_t)MAX_W * MAX_H * BYTES_PER_PIXEL,
	    now = (size_t)mode.hdisplay * mode.vdisplay * BYTES_PER_PIXEL;

	return now > max ? now : max;
}

/*
 * fbmode=WIDTHxHEIGHT or WIDTHxHEIGHT@HZ - an argument of the service, or a
 * boot parameter of that name (env_get_param() looks at both): the mode to
 * show instead of the monitor's preferred one.  It is looked for in the
 * monitor's own list only - a monitor shows what it says it can, and
 * anything else is a gamble on its scaler - with the rate nearest to HZ
 * (60 when not given).  1 and the numbers if asked for, 0 if not or not
 * understood.
 */
static int
asked_mode(unsigned *w, unsigned *h, unsigned *hz)
{
	char buf[32], *e;

	if (env_get_param("fbmode", buf, sizeof(buf)) != OK)
		return 0;
	*w = (unsigned)strtoul(buf, &e, 10);
	if (*e == 'x') {
		*h = (unsigned)strtoul(e + 1, &e, 10);
		*hz = 60;
		if (*e == '@')
			*hz = (unsigned)strtoul(e + 1, &e, 10);
		if (*e == '\0' && *w > 0 && *h > 0 && *hz > 0)
			return 1;
	}
	log_warn(&fb_log, "fbmode=%s: not WIDTHxHEIGHT[@HZ], ignored\n", buf);
	return 0;
}

/*
 * The monitor's modes this driver can show, "1024x768@60*" for the one on
 * the screen: the choice fbmode= has, in the report fblog prints (the
 * journal of bring-up scrolls away within minutes).
 */
static void
modes_line(char *buf, size_t len)
{
	const struct videomode *list;
	size_t n;
	int i, nm;

	list = mode_list(&nm);
	n = (size_t)snprintf(buf, len, "modes:");
	for (i = 0; i < nm && n < len; i++) {
		const struct videomode *m = &list[i];

		if (!mode_usable(m))
			continue;
		n += (size_t)snprintf(buf + n, len - n, " %dx%d@%u%s",
		    m->hdisplay, m->vdisplay, mode_hz(m),
		    same_mode(m, &mode) ? "*" : "");
	}
}

/*
 * What the connector has: is a monitor there, and if so its EDID, base
 * block and CEA-861 extension - the list mode_list() then gives.  1 for a
 * monitor whose list was read, 0 for none, -1 for one whose EDID did not
 * read (yet: some answer a moment after their hot plug line rises); "say"
 * whether to log that.  The virtual display's "monitor" is the one
 * FBIOC_VPLUG last plugged in.
 */
static int
read_monitor(int say)
{
	uint8_t block[128];
	int i;

	have_edid = 0;
	if (virtual_mode) {
		if (virtual_plug == FB_VPLUG_NONE)
			return 0;
		virtual_read = virtual_plug;
		log_info(&fb_log, "virtual monitor %u\n", virtual_read);
		return 1;
	}
	if (!dwhdmi_hpd()) {
		if (say)
			log_info(&fb_log, "no monitor on the connector (hot "
			    "plug low)\n");
		return 0;
	}
	memset(&edid, 0, sizeof(edid));
	if (dwhdmi_read_edid(0, block) != OK || edid_is_valid(block) != 0 ||
	    edid_parse(block, &edid) != 0) {
		if (say)
			log_warn(&fb_log, "a monitor is plugged in, but its "
			    "EDID did not read\n");
		return -1;
	}
	have_edid = 1;
	log_info(&fb_log, "monitor: %s %s (%s), EDID %d.%d, %d mode(s)\n",
	    edid.edid_vendorname, edid.edid_productname, edid.edid_serial,
	    edid.edid_version, edid.edid_revision, edid.edid_nmodes);
	add_cea_modes();
	for (i = 0; i < edid.edid_nmodes; i++)
		log_info(&fb_log, "  %dx%d@%u, %d kHz%s%s\n",
		    edid.edid_modes[i].hdisplay, edid.edid_modes[i].vdisplay,
		    mode_hz(&edid.edid_modes[i]), edid.edid_modes[i].dot_clock,
		    &edid.edid_modes[i] == edid.edid_preferred_mode ?
		    ", preferred" : "",
		    mode_usable(&edid.edid_modes[i]) ? "" : ", not made here");
	return 1;
}

/*
 * The mode to show, out of what read_monitor() found: fbmode= if the list
 * has it; the monitor's preferred, if this driver makes it; otherwise the
 * largest it lists that this driver makes (of one size, the rate nearest
 * to 60); the first of the virtual display's; or the reference mode.
 */
static const struct videomode *
pick_mode(const char **why)
{
	const struct videomode *list, *m, *best = NULL;
	unsigned w, h, hz, d, dbest = 0;
	int i, n, a, abest = 0;

	if (asked_mode(&w, &h, &hz)) {
		if ((m = find_mode(w, h, hz)) != NULL) {
			*why = "asked for (fbmode)";
			return m;
		}
		log_warn(&fb_log, "fbmode=%ux%u: not among the modes this "
		    "driver makes for this screen; choosing as usual\n", w, h);
	}
	list = mode_list(&n);
	if (virtual_mode) {
		*why = "the virtual monitor's first";
		return &list[0];
	}
	if (!have_edid) {
		*why = "the reference mode (no list)";
		return &mode_720p;
	}
	if (edid.edid_preferred_mode != NULL &&
	    mode_usable(edid.edid_preferred_mode)) {
		*why = "its preferred mode";
		return edid.edid_preferred_mode;
	}
	for (i = 0; i < n; i++) {
		if (!mode_usable(&list[i]))
			continue;
		d = mode_hz(&list[i]);
		d = d > 60 ? d - 60 : 60 - d;
		a = list[i].hdisplay * list[i].vdisplay;
		if (best == NULL || a > abest || (a == abest && d < dbest)) {
			best = &list[i];
			abest = a;
			dbest = d;
		}
	}
	if (best == NULL) {
		*why = "the reference mode (none of the monitor's is made here)";
		return &mode_720p;
	}
	*why = "the largest it lists that this driver makes";
	return best;
}

static void
choose_mode(void)
{
	const char *why;

	hdmi_mode = 0;
	(void)read_monitor(1);
	preferred = *pick_mode(&why);
	mode = preferred;
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
	buf_bytes = buffer_bytes();

	/*
	 * Physically contiguous - the window fetches it as one range - and
	 * below 4 GiB, which the window's address register needs and every
	 * byte of RAM on this SoC is.  The memory is ordinary cacheable RAM;
	 * arch_fb_flush() cleans what was drawn, because the controller
	 * reads behind the caches.  As large as the largest mode of the list:
	 * FBIOC_SETMODE changes the mode, never the buffers.
	 */
	if ((v = alloc_contig(buf_bytes, AC_ALIGN64K, &fb_phys)) == NULL) {
		log_warn(&fb_log, "cannot allocate %zu contiguous bytes for "
		    "the frame buffer\n", buf_bytes);
		return ENOMEM;
	}
	fb_vir = (vir_bytes)v;
	buf_vir[0] = fb_vir;		/* before the first flush: it uses these */
	buf_phys[0] = fb_phys;
	nbuf = 1;
	memset(v, 0, buf_bytes);
	{
		/* What a whole-frame clean costs: the console pays it per scroll. */
		u64_t t0, t1;

		read_frclock_64(&t0);
		arch_fb_flush(0, 0, fb_size);
		read_frclock_64(&t1);
		log_info(&fb_log, "cleaning the whole frame out of the cache: "
		    "%u us\n", frclock_64_to_micros(delta_frclock_64(t0, t1)));
	}
	log_info(&fb_log, "frame buffer %ux%u, stride %u, %zu bytes (buffers "
	    "of %zu) at physical 0x%lx\n", mode.hdisplay, mode.vdisplay,
	    fb_stride, fb_size, buf_bytes, (unsigned long)fb_phys);
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

/*
 * A buffer the GPU drew, to the processor: clean and invalidate it whole,
 * in pieces.  Invalidate for the stale lines a processor may hold - fetched
 * before the GPU wrote, or by speculation - and clean first, which writes
 * nothing back here (no processor wrote it since) but is the safe one of
 * the two whatever the caches hold.
 */
static void
drop_buffer(int b)
{
	size_t off, n;

	if (!gpu_drawn[b])
		return;
	for (off = 0; off < buf_bytes; off += n) {
		n = buf_bytes - off < FLUSH_PIECE ? buf_bytes - off :
		    FLUSH_PIECE;
		(void)sys_cachectl(CACHE_CLEAN_INVALIDATE,
		    (void *)(buf_vir[b] + off), n);
	}
	gpu_drawn[b] = 0;
}

/*
 * The scanout buffers to DS, for the GPU service (meecho_drm.h): their
 * physical addresses - below 4 GiB, as the window needs - and size.  The
 * count goes last: a reader that sees n sees the n addresses.
 */
static void
publish_buffers(void)
{
	char key[DS_MAX_KEYLEN];
	int i;

	(void)ds_publish_u32(MEECHO_DS_SCANOUT_SIZE, (u32_t)buf_bytes,
	    DSF_OVERWRITE);
	for (i = 0; i < nbuf; i++) {
		snprintf(key, sizeof(key), MEECHO_DS_SCANOUT, i);
		(void)ds_publish_u32(key, (u32_t)buf_phys[i], DSF_OVERWRITE);
		snprintf(key, sizeof(key), MEECHO_DS_SCANOUT_VIR, i);
		(void)ds_publish_u32(key, (u32_t)buf_vir[i], DSF_OVERWRITE);
	}
	(void)ds_publish_u32(MEECHO_DS_SCANOUT_N, (u32_t)nbuf, DSF_OVERWRITE);
}

/*
 * Put buffer b on the screen at the next frame start.  With three buffers
 * the call does not wait for it (see vop2_scanout() and FBIOC_FLIP); with
 * fewer it returns when b is there.
 */
static int
show(int b)
{
	int r = OK;

	if (!virtual_mode &&
	    (r = vop2_scanout(buf_phys[b], nbuf < FB_MAX_BUFFERS)) != OK)
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
		if ((v = alloc_contig(buf_bytes, AC_ALIGN64K,
		    &buf_phys[nbuf])) == NULL) {
			log_warn(&fb_log, "no memory for scanout buffer %d\n",
			    nbuf);
			break;
		}
		memset(v, 0, buf_bytes);
		buf_vir[nbuf] = (vir_bytes)v;
		flush_rows(nbuf, 0, mode.vdisplay);
		nbuf++;
	}

	memset(&m, 0, sizeof(m));
	m.width = mode.hdisplay;
	m.height = mode.vdisplay;
	m.stride = fb_stride;
	m.format = FB_FORMAT_XRGB8888;
	m.size = (uint32_t)buf_bytes;	/* all of it: a larger mode may come */
	m.nbuf = nbuf;
	m.shown = shown;
	for (i = 0; i < nbuf; i++) {
		v = vm_remap(user_ep, sef_self(), NULL, (void *)buf_vir[i],
		    buf_bytes);
		if (v == MAP_FAILED) {
			log_warn(&fb_log, "cannot map buffer %d into %d\n", i,
			    user_ep);
			return ENOMEM;
		}
		m.addr[i] = (uint64_t)(uintptr_t)v;
	}
	publish_buffers();
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
	if (f.buf >= (uint32_t)nbuf || (f.flags & ~FB_FLIP_GPU))
		return EINVAL;
	if (f.flags & FB_FLIP_GPU)
		gpu_drawn[f.buf] = 1;	/* nothing in the caches to clean */
	else if (f.y1 > f.y0)
		flush_rows((int)f.buf, f.y0, f.y1);
	else
		flush_rows((int)f.buf, 0, mode.vdisplay);
	return show((int)f.buf);
}

/*
 * The cursor as "cur" says, on the controller for mode m.  The virtual
 * display has none: there the cursor exists in what read(2) answers only.
 */
static void
put_cursor(const struct videomode *m)
{
	if (virtual_mode || ncur == 0)
		return;
	(void)vop2_cursor((cur.flags & FB_CURSOR_SHOW) != 0, cur_phys[cur.buf],
	    CURSOR_STRIDE, cur.x, cur.y, cur.w, cur.h, m->hdisplay,
	    m->vdisplay);
}

/* FBIOC_CURSOR_MAP: the cursor's pictures, mapped into the owner. */
static int
do_cursor_map(endpoint_t ep, cp_grant_id_t gid, endpoint_t user_ep)
{
	struct fb_cursor_map m;
	void *v;
	int i;

	if (user_ep != owner)
		return EPERM;
	while (ncur < FB_CURSOR_BUFFERS) {
		/* Below 4 GiB, as the window's address register needs. */
		if ((v = alloc_contig(CURSOR_BYTES, 0, &cur_phys[ncur])) ==
		    NULL) {
			log_warn(&fb_log, "no memory for cursor buffer %d\n",
			    ncur);
			return ENOMEM;
		}
		memset(v, 0, CURSOR_BYTES);
		cur_vir[ncur] = (vir_bytes)v;
		(void)sys_cachectl(CACHE_CLEAN, v, CURSOR_BYTES);
		ncur++;
	}

	memset(&m, 0, sizeof(m));
	m.max_w = m.max_h = FB_CURSOR_MAX;
	m.stride = CURSOR_STRIDE;
	m.format = FB_FORMAT_ARGB8888;
	m.nbuf = ncur;
	for (i = 0; i < ncur; i++) {
		v = vm_remap(user_ep, sef_self(), NULL, (void *)cur_vir[i],
		    CURSOR_BYTES);
		if (v == MAP_FAILED) {
			log_warn(&fb_log, "cannot map cursor buffer %d into %d\n",
			    i, user_ep);
			return ENOMEM;
		}
		m.addr[i] = (uint64_t)(uintptr_t)v;
	}
	log_info(&fb_log, "%d cursor buffer(s) mapped into %d\n", ncur,
	    user_ep);
	return sys_safecopyto(ep, gid, 0, (vir_bytes)&m, sizeof(m));
}

/* FBIOC_CURSOR: show, move, change or hide the cursor. */
static int
do_cursor(endpoint_t ep, cp_grant_id_t gid, endpoint_t user_ep)
{
	struct fb_cursor c;
	int r;

	if (user_ep != owner)
		return EPERM;
	if ((r = sys_safecopyfrom(ep, gid, 0, (vir_bytes)&c, sizeof(c))) != OK)
		return r;
	if (c.flags & ~(FB_CURSOR_SHOW | FB_CURSOR_IMAGE))
		return EINVAL;
	if ((c.flags & FB_CURSOR_SHOW) && (c.buf >= (uint32_t)ncur ||
	    c.w == 0 || c.h == 0 || c.w > FB_CURSOR_MAX ||
	    c.h > FB_CURSOR_MAX))
		return EINVAL;
	if ((c.flags & FB_CURSOR_IMAGE) && c.buf < (uint32_t)ncur)
		(void)sys_cachectl(CACHE_CLEAN, (void *)cur_vir[c.buf],
		    CURSOR_BYTES);
	if (!(c.flags & FB_CURSOR_SHOW))
		c.buf = cur.buf;		/* nothing else to keep */
	c.flags &= FB_CURSOR_SHOW;
	cur = c;
	cur_moves++;
	put_cursor(&mode);
	return OK;
}

/* The owner is gone, or the console comes back: no cursor. */
static void
hide_cursor(void)
{
	if (!(cur.flags & FB_CURSOR_SHOW))
		return;
	cur.flags = 0;
	put_cursor(&mode);
}

/*
 * What read(2) answers is the screen, so the cursor is in it: after
 * fb_read() has copied bytes pos..pos+size-1 of the frame on the screen to
 * the reader, the rows of it under the cursor are copied again, blended as
 * the controller blends them - Cd = Cs + (1 - As) Cd, the cursor
 * premultiplied.
 */
void
arch_fb_read_overlay(int minor, u64_t pos, size_t size, endpoint_t ep,
	cp_grant_id_t gid)
{
	uint32_t row[FB_CURSOR_MAX];
	const uint32_t *src, *img;
	uint32_t d, s, a, v, out;
	u64_t start, end, seg0, seg1;
	int x0, y0, x1, y1, x, y, c, sh;
	size_t n;

	if (minor != 0 || status <= 0 || !(cur.flags & FB_CURSOR_SHOW) ||
	    ncur == 0 || size == 0)
		return;
	x0 = cur.x < 0 ? 0 : cur.x;
	y0 = cur.y < 0 ? 0 : cur.y;
	x1 = cur.x + (int)cur.w > mode.hdisplay ? mode.hdisplay :
	    cur.x + (int)cur.w;
	y1 = cur.y + (int)cur.h > mode.vdisplay ? mode.vdisplay :
	    cur.y + (int)cur.h;
	if (x1 <= x0 || y1 <= y0)
		return;
	start = pos;
	end = pos + size;
	for (y = y0; y < y1; y++) {
		seg0 = (u64_t)y * fb_stride + (u64_t)x0 * BYTES_PER_PIXEL;
		seg1 = (u64_t)y * fb_stride + (u64_t)x1 * BYTES_PER_PIXEL;
		if (seg1 <= start || seg0 >= end)
			continue;
		if (seg0 < start)	/* whole pixels only: round inwards */
			seg0 += (start - seg0 + BYTES_PER_PIXEL - 1) /
			    BYTES_PER_PIXEL * BYTES_PER_PIXEL;
		if (seg1 > end)
			seg1 -= (seg1 - end + BYTES_PER_PIXEL - 1) /
			    BYTES_PER_PIXEL * BYTES_PER_PIXEL;
		if (seg1 <= seg0)
			continue;
		src = (const uint32_t *)(buf_vir[shown] + (size_t)seg0);
		x = (int)((seg0 - (u64_t)y * fb_stride) / BYTES_PER_PIXEL);
		img = (const uint32_t *)(cur_vir[cur.buf] +
		    (size_t)(y - cur.y) * CURSOR_STRIDE) + (x - cur.x);
		n = (size_t)(seg1 - seg0) / BYTES_PER_PIXEL;
		for (c = 0; c < (int)n; c++) {
			d = src[c];
			s = img[c];
			a = 255 - (s >> 24);
			out = 0;
			for (sh = 0; sh < 24; sh += 8) {
				v = ((s >> sh) & 0xff) +
				    (((d >> sh) & 0xff) * a + 127) / 255;
				out |= (v > 255 ? 255 : v) << sh;
			}
			row[c] = out | (d & 0xff000000u);
		}
		(void)sys_safecopyto(ep, gid, (vir_bytes)(seg0 - start),
		    (vir_bytes)row, n * BYTES_PER_PIXEL);
	}
}

/*
 * The hardware for mode m, in bring-up's order: the pixel clock, then the
 * display controller - its port in standby while it is described, and
 * scanning the buffer on the screen - then the encoder and its PHY last.
 * The geometry (fb_stride, fb_size) is m's from here on.
 */
static int
program_mode(struct videomode *m)
{
	unsigned got;
	int r;

	fb_stride = m->hdisplay * BYTES_PER_PIXEL;
	fb_size = (size_t)fb_stride * m->vdisplay;
	if (virtual_mode)
		return OK;
	if ((r = rk_vo_clocks(&disp, m->dot_clock, &got)) != OK)
		return r;
	m->dot_clock = got;
	if ((r = vop2_set_mode(m, buf_phys[shown], fb_stride)) != OK)
		return r;
	put_cursor(m);			/* vop2_set_mode() took it away */
	return dwhdmi_set_mode(m, hdmi_mode);
}

/*
 * Another mode of the list, while the driver runs: the buffer on the screen
 * cleared to black in the new geometry, the hardware set for it, and the
 * console given the new size.  The buffers stay (buffer_bytes()).  If the
 * hardware does not take it, the old mode goes back on.
 */
static int
change_mode(const struct videomode *want)
{
	struct videomode old = mode;
	int r;

	if (same_mode(want, &mode))
		return OK;
	if ((size_t)want->hdisplay * want->vdisplay * BYTES_PER_PIXEL >
	    buf_bytes)
		return EINVAL;			/* not a mode of the list */

	mode = *want;
	memset((void *)buf_vir[shown], 0,
	    (size_t)mode.hdisplay * BYTES_PER_PIXEL * mode.vdisplay);
	flush_rows(shown, 0, mode.vdisplay);
	if ((r = program_mode(&mode)) != OK) {
		log_warn(&fb_log, "mode %dx%d@%u did not take (%d); back to "
		    "%dx%d@%u\n", want->hdisplay, want->vdisplay, mode_hz(want),
		    r, old.hdisplay, old.vdisplay, mode_hz(&old));
		mode = old;
		if (program_mode(&mode) != OK)
			log_warn(&fb_log, "and the old mode did not take either\n");
		fbcon_resize();
		return EIO;
	}
	log_info(&fb_log, "mode %dx%d@%u, %d kHz: changed from %dx%d@%u\n",
	    mode.hdisplay, mode.vdisplay, mode_hz(&mode), mode.dot_clock,
	    old.hdisplay, old.vdisplay, mode_hz(&old));
	fbcon_resize();
	return OK;
}

/* FBIOC_MODES: the list, and which of it is on the screen. */
static int
do_modes(endpoint_t ep, cp_grant_id_t gid)
{
	static struct fb_modes ms;
	const struct videomode *list;
	int i, n;

	memset(&ms, 0, sizeof(ms));
	ms.current = FB_MAX_MODES;		/* none of the list, until found */
	ms.preferred = FB_MAX_MODES;
	ms.generation = generation;
	list = mode_list(&n);
	for (i = 0; i < n && ms.n < FB_MAX_MODES; i++) {
		if (!mode_usable(&list[i]))
			continue;
		if (same_mode(&list[i], &mode))
			ms.current = ms.n;
		if (same_mode(&list[i], &preferred))
			ms.preferred = ms.n;
		ms.mode[ms.n].width = (uint32_t)list[i].hdisplay;
		ms.mode[ms.n].height = (uint32_t)list[i].vdisplay;
		ms.mode[ms.n].hz = mode_hz(&list[i]);
		ms.mode[ms.n].khz = (uint32_t)list[i].dot_clock;
		ms.n++;
	}
	if (ms.current >= ms.n)
		ms.current = ms.n;
	if (ms.preferred >= ms.n)
		ms.preferred = ms.n;
	return sys_safecopyto(ep, gid, 0, (vir_bytes)&ms, sizeof(ms));
}

/* FBIOC_SETMODE: the owner's, or anybody's while nobody owns the screen. */
static int
do_setmode(endpoint_t ep, cp_grant_id_t gid, endpoint_t user_ep)
{
	struct fb_setmode s;
	const struct videomode *m;
	int r;

	if (owner != NONE && user_ep != owner)
		return EPERM;
	if ((r = sys_safecopyfrom(ep, gid, 0, (vir_bytes)&s, sizeof(s))) != OK)
		return r;
	if ((m = find_mode(s.width, s.height, s.hz != 0 ? s.hz : 60)) == NULL)
		return EINVAL;
	if ((r = change_mode(m)) != OK)
		return r;
	s.width = (uint32_t)mode.hdisplay;
	s.height = (uint32_t)mode.vdisplay;
	s.hz = mode_hz(&mode);
	s.stride = fb_stride;
	return sys_safecopyto(ep, gid, 0, (vir_bytes)&s, sizeof(s));
}

/*
 * A monitor plugged in or taken out after bring-up.  The hot plug line is
 * read on every tick, ten a second - one register - and a change counts
 * once the line has held it for HPD_STEADY ticks: a plug bounces.  A
 * monitor that has come is asked for its EDID on the following ticks, up
 * to EDID_TRIES of them, since some answer only a moment after their line
 * rises; one that never answers gets the reference mode's list.
 */
#define HPD_STEADY	3
#define EDID_TRIES	10

static int hpd_known;			/* the line as last acted on */
static int hpd_ticks;			/* ticks it has read otherwise */
static int edid_left;			/* tries left for a new monitor */

static int
hpd_line(void)
{
	return virtual_mode ? virtual_plug != FB_VPLUG_NONE : dwhdmi_hpd();
}

/*
 * A monitor is there and its list read: its mode.  Of the same size as the
 * one on the screen, the link is set up once more - a sink that came after
 * the signal may want that, and the frames do not change, so it is safe
 * under a compositor too.  Of another size, the change is the driver's
 * while nobody owns the screen (the console follows); an owner is told by
 * FBIOC_MODES - generation moved, preferred is this - and changes it
 * itself, so that its frames and the screen change size together.
 */
static void
monitor_came(void)
{
	const char *why;
	int r;

	generation++;
	preferred = *pick_mode(&why);
	if (same_mode(&preferred, &mode)) {
		if (!virtual_mode && (r = program_mode(&mode)) != OK)
			log_warn(&fb_log, "monitor %u: the link did not come "
			    "up again (%d)\n", generation, r);
		log_info(&fb_log, "monitor %u: %dx%d@%u as before (%s)\n",
		    generation, mode.hdisplay, mode.vdisplay, mode_hz(&mode),
		    why);
		return;
	}
	if (owner != NONE) {
		log_info(&fb_log, "monitor %u: %dx%d@%u (%s), for the owner "
		    "of the screen to take\n", generation, preferred.hdisplay,
		    preferred.vdisplay, mode_hz(&preferred), why);
		return;
	}
	if ((r = change_mode(&preferred)) != OK)
		log_warn(&fb_log, "monitor %u: %dx%d@%u did not take (%d)\n",
		    generation, preferred.hdisplay, preferred.vdisplay,
		    mode_hz(&preferred), r);
	else
		log_info(&fb_log, "monitor %u: %s\n", generation, why);
}

void
arch_fb_tick(int minor)
{
	int hpd, r;

	if (minor != 0 || status <= 0)
		return;
	if (edid_left > 0) {			/* a monitor came: its list */
		r = read_monitor(edid_left == 1);
		if (r == 0)			/* gone again */
			edid_left = 0;
		else if (r > 0 || --edid_left == 0) {
			edid_left = 0;
			monitor_came();
		}
		return;
	}
	hpd = hpd_line();
	if (hpd == hpd_known) {
		hpd_ticks = 0;
		return;
	}
	if (++hpd_ticks < HPD_STEADY)
		return;
	hpd_known = hpd;
	hpd_ticks = 0;
	if (!hpd) {
		log_info(&fb_log, "the monitor was taken out; the mode stays "
		    "%dx%d@%u\n", mode.hdisplay, mode.vdisplay, mode_hz(&mode));
		return;
	}
	log_info(&fb_log, "a monitor was plugged in\n");
	edid_left = EDID_TRIES;
}

void
arch_fb_release(int minor)
{
	int b;

	if (minor != 0 || status <= 0)
		return;
	if (owner != NONE)
		log_info(&fb_log, "the screen is given back by %d\n", owner);
	owner = NONE;
	/* The console draws with the processor, the next owner may too. */
	for (b = 0; b < nbuf; b++)
		drop_buffer(b);
	hide_cursor();
	if (shown != 0)
		(void)show(0);
}

/* The cursor as the driver was last asked for it. */
static void
report_cursor(void)
{
	if (cur.flags & FB_CURSOR_SHOW)
		log_info(&fb_log, "cursor: at %d,%d, %ux%u, buffer %u; %lu "
		    "update(s)\n", cur.x, cur.y, cur.w, cur.h, cur.buf,
		    cur_moves);
	else
		log_info(&fb_log, "cursor: hidden; %lu update(s)\n", cur_moves);
	if (!virtual_mode && ncur > 0)
		vop2_report_cursor();
}

/* Which buffer the controller's window names, by its address register. */
static void
report_window(void)
{
	uint32_t a;
	int i, pending;

	a = vop2_window_addr(&pending);
	for (i = 0; i < nbuf; i++)
		if (buf_phys[i] == (phys_bytes)a)
			break;
	if (i < nbuf)
		log_info(&fb_log, "window at 0x%08x: buffer %d%s\n", a, i,
		    pending ? " (commit pending)" : "");
	else
		log_info(&fb_log, "window at 0x%08x: none of the buffers%s\n",
		    a, pending ? " (commit pending)" : "");
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
		modes_line(stats, sizeof(stats));
		log_info(&fb_log, "%s\n", stats);
		report_cursor();
		fbcon_stats(stats, sizeof(stats));
		log_info(&fb_log, "%s\n", stats);
		return;
	}
	log_info(&fb_log, "state: monitor %s, PHY %s, mode %dx%d@%u, buffer %d "
	    "of %d shown, owner %d\n",
	    dwhdmi_hpd() ? "plugged in" : "absent",
	    dwhdmi_phy_locked() ? "locked" : "NOT locked",
	    mode.hdisplay, mode.vdisplay, mode_hz(&mode), shown, nbuf, owner);
	modes_line(stats, sizeof(stats));
	log_info(&fb_log, "%s\n", stats);
	(void)vop2_scanning(&line);
	report_window();
	report_cursor();
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
		/* No hardware: a frame in memory, 720p or fbmode=, no more. */
		const char *why;

		virtual_mode = 1;
		(void)read_monitor(1);
		preferred = *pick_mode(&why);
		mode = preferred;
		hpd_known = hpd_line();
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
	hpd_known = hpd_line();		/* from here on, arch_fb_tick() */

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
	drop_buffer(shown);
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
	if (request == FBIOC_MAP || request == FBIOC_FLIP ||
	    request == FBIOC_MODES || request == FBIOC_SETMODE ||
	    request == FBIOC_CURSOR_MAP || request == FBIOC_CURSOR) {
		if (status <= 0)
			return ENXIO;
		switch (request) {
		case FBIOC_MAP:		return do_map(ep, gid, user_ep);
		case FBIOC_FLIP:	return do_flip(ep, gid, user_ep);
		case FBIOC_MODES:	return do_modes(ep, gid);
		case FBIOC_CURSOR_MAP:	return do_cursor_map(ep, gid, user_ep);
		case FBIOC_CURSOR:	return do_cursor(ep, gid, user_ep);
		default:		return do_setmode(ep, gid, user_ep);
		}
	}
	if (request == FBIOC_VPLUG) {
		/* The virtual display's monitor; arch_fb_tick() takes it up. */
		uint32_t plug;
		int r;

		if (status <= 0 || !virtual_mode)
			return ENOTTY;
		if ((r = sys_safecopyfrom(ep, gid, 0, (vir_bytes)&plug,
		    sizeof(plug))) != OK)
			return r;
		if (plug > FB_VPLUG_SMALL)
			return EINVAL;
		virtual_plug = plug;
		return OK;
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
