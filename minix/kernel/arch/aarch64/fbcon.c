/*
 * The kernel's console on a frame buffer someone else set up.
 *
 * A machine may hand over with its display already running and say so in the
 * device tree: a "simple-framebuffer" node - in /chosen, usually - naming the
 * memory being scanned out, its geometry and its pixel format. The Mi Pad 4
 * is the case that asked for this: its Qualcomm ABL leaves the panel lit with
 * the splash screen, and the UART is on test pads under the back cover. So
 * the frame buffer is the only place the kernel can say anything at all, and
 * it has to be able to from the first line of pre_init(), before any driver
 * exists - the same as the serial console, and next to it rather than instead
 * of it: kernel output goes to both.
 *
 * Nothing about the display hardware is touched. Whoever lit the panel keeps
 * it lit; this only writes pixels into the memory being shown. A machine with
 * no such node (QEMU's virt, the CB2 - its U-Boot has no video) gets no frame
 * buffer console and loses nothing.
 *
 * The memory is mapped as a device range (kern_phys_map_ptr(), like the UART),
 * so a store reaches the display without cache maintenance. That makes reads
 * slow, so nothing is ever read back: the screen does not scroll, it wraps -
 * the line after the last is the first one again, and the line the cursor
 * moves onto is cleared, so the newest line always sits above a blank one.
 *
 * The font is Spleen 8x16 (Frederic Cambus, BSD 2-clause), the same copy from
 * sys/dev/wsfont the fb driver uses. On a dense panel it is drawn doubled.
 */

#include <string.h>
#include <sys/types.h>
#include <minix/type.h>
#include "kernel/kernel.h"
#include "kernel/vm.h"
#include "arch_proto.h"
#include <minix/fdt.h>

/* What NetBSD's <dev/wscons/wsconsio.h> says of a font, as far as needed. */
#define WSDISPLAY_FONTENC_ISO	0
#define WSDISPLAY_FONTORDER_L2R	1
struct wsdisplay_font {
	const char *name;
	int firstchar, numchars, encoding;
	u_int fontwidth, fontheight, stride;
	int bitorder, byteorder;
	void *data;
};
#include <spleen8x16.h>

#define FONT_W		8
#define FONT_H		16

/* Colours as 8-bit channels; packed per format by pixel(). */
#define FG_R	0xd8
#define FG_G	0xd8
#define FG_B	0xd0
#define BG_R	0x10
#define BG_G	0x14
#define BG_B	0x1c

enum fb_format { FB_NONE, FB_XRGB8888, FB_XBGR8888, FB_RGB565 };

static struct {
	vir_bytes base;		/* page-aligned; rewritten after the MMU move */
	vir_bytes offset;	/* of the first pixel within that page */
	unsigned width, height, stride, bpp;
	enum fb_format format;
	unsigned scale;		/* font drawn this many times its size */
	unsigned cols, rows;
	unsigned col, row;
	u32_t fg, bg;
	int owned;		/* a driver draws here now (fbcon_owned()) */
} fb;

static kern_phys_map fb_phys_map;

struct fb_scan {
	u64_t addr, size;
	unsigned width, height, stride;
	enum fb_format format;
	int found;
};

/*===========================================================================*
 *				prop_u32				     *
 *===========================================================================*/
static int
prop_u32(const struct fdt_node *node, const char *name, unsigned *v)
{
	const void *p;
	unsigned len;

	if ((p = fdt_getprop(node, name, &len)) == NULL || len != 4)
		return 0;
	*v = (unsigned)fdt_read_cells(p, 1);
	return 1;
}

/*===========================================================================*
 *				scan_fb					     *
 *===========================================================================*/
static int
scan_fb(void *cookie, int depth, const char *name, const struct fdt_node *node)
{
	struct fb_scan *s = cookie;
	const char *p;
	unsigned len;

	(void)depth;
	(void)name;

	if (!fdt_node_is_compatible(node, "simple-framebuffer"))
		return 0;

	/* A loader that did not light the display leaves the node disabled. */
	if ((p = fdt_getprop(node, "status", &len)) != NULL && len > 0 &&
	    strcmp(p, "okay") != 0 && strcmp(p, "ok") != 0)
		return 0;

	if (fdt_node_reg(node, 0, &s->addr, &s->size) != 0 ||
	    !prop_u32(node, "width", &s->width) ||
	    !prop_u32(node, "height", &s->height) ||
	    !prop_u32(node, "stride", &s->stride))
		return 0;

	if ((p = fdt_getprop(node, "format", &len)) == NULL || len == 0)
		return 0;
	if (!strcmp(p, "a8r8g8b8") || !strcmp(p, "x8r8g8b8"))
		s->format = FB_XRGB8888;
	else if (!strcmp(p, "a8b8g8r8") || !strcmp(p, "x8b8g8r8"))
		s->format = FB_XBGR8888;
	else if (!strcmp(p, "r5g6b5"))
		s->format = FB_RGB565;
	else
		return 0;	/* a format nobody here can draw in */

	s->found = 1;
	return 1;
}

/*===========================================================================*
 *				pixel					     *
 *===========================================================================*/
static u32_t
pixel(unsigned r, unsigned g, unsigned b)
{
	switch (fb.format) {
	case FB_XRGB8888:
		return 0xff000000u | r << 16 | g << 8 | b;
	case FB_XBGR8888:
		return 0xff000000u | b << 16 | g << 8 | r;
	case FB_RGB565:
		return (r >> 3) << 11 | (g >> 2) << 5 | b >> 3;
	default:
		return 0;
	}
}

/*===========================================================================*
 *				put_px					     *
 *===========================================================================*/
static void
put_px(vir_bytes line, unsigned x, u32_t v)
{
	if (fb.bpp == 4)
		((volatile u32_t *)line)[x] = v;
	else
		((volatile u16_t *)line)[x] = (u16_t)v;
}

/*===========================================================================*
 *				fill_rows				     *
 *===========================================================================*/
/* Paint text rows [first, first + n) with the background. */
static void
fill_rows(unsigned first, unsigned n)
{
	unsigned y, x, y0 = first * FONT_H * fb.scale;
	unsigned y1 = (first + n) * FONT_H * fb.scale;

	if (y1 > fb.height)
		y1 = fb.height;
	for (y = y0; y < y1; y++) {
		vir_bytes line = fb.base + fb.offset + (vir_bytes)y * fb.stride;

		for (x = 0; x < fb.width; x++)
			put_px(line, x, fb.bg);
	}
}

/*===========================================================================*
 *				draw_char				     *
 *===========================================================================*/
static void
draw_char(unsigned col, unsigned row, unsigned char c)
{
	const u_char *glyph;
	unsigned gy, gx, sy, sx;
	unsigned x0 = col * FONT_W * fb.scale, y0 = row * FONT_H * fb.scale;

	if (c < spleen8x16.firstchar ||
	    c >= spleen8x16.firstchar + spleen8x16.numchars)
		c = '?';
	/*
	 * spleen8x16_data by name, not spleen8x16.data: that is a pointer in
	 * .data, and it holds the upper-half address, which is no address at
	 * all before the MMU is on - and the first characters are drawn then.
	 * The array named directly is reached PC-relatively, right either way.
	 */
	glyph = spleen8x16_data + (c - spleen8x16.firstchar) * FONT_H;

	for (gy = 0; gy < FONT_H; gy++) {
		unsigned bits = glyph[gy];

		for (sy = 0; sy < fb.scale; sy++) {
			vir_bytes line = fb.base + fb.offset +
			    (vir_bytes)(y0 + gy * fb.scale + sy) * fb.stride;

			for (gx = 0; gx < FONT_W; gx++) {
				u32_t v = (bits & (0x80 >> gx)) ? fb.fg : fb.bg;

				for (sx = 0; sx < fb.scale; sx++)
					put_px(line, x0 + gx * fb.scale + sx,
					    v);
			}
		}
	}
}

/*===========================================================================*
 *				new_line				     *
 *===========================================================================*/
static void
new_line(void)
{
	fb.col = 0;
	if (++fb.row == fb.rows)
		fb.row = 0;
	/* Clear the line we land on, so the newest text sits above a gap. */
	fill_rows(fb.row, 1);
}

/*===========================================================================*
 *				fbcon_init				     *
 *===========================================================================*/
void
fbcon_init(phys_bytes dtb)
{
	struct fb_scan s;
	phys_bytes base, end;

	memset(&s, 0, sizeof(s));
	if (dtb == 0 || !fdt_valid((const void *)dtb))
		return;
	(void)fdt_walk((const void *)dtb, scan_fb, &s);
	if (!s.found || s.width == 0 || s.height == 0)
		return;

	fb.bpp = (s.format == FB_RGB565) ? 2 : 4;
	if ((u64_t)s.stride * s.height > s.size || s.stride < s.width * fb.bpp)
		return;		/* the node contradicts itself; draw nothing */

	fb.format = s.format;
	fb.width = s.width;
	fb.height = s.height;
	fb.stride = s.stride;

	/*
	 * Doubled on anything a thousand pixels across or more: Spleen 8x16
	 * on the Mi Pad 4's 1200x1920 eight-inch panel is under a millimetre
	 * wide, and 75 columns that can be read beat 150 that cannot.
	 */
	fb.scale = (fb.width < fb.height ? fb.width : fb.height) >= 1000 ? 2 : 1;
	fb.cols = fb.width / (FONT_W * fb.scale);
	fb.rows = fb.height / (FONT_H * fb.scale);
	if (fb.cols == 0 || fb.rows == 0)
		return;

	base = (phys_bytes)s.addr & ~(phys_bytes)(AARCH64_PAGE_SIZE - 1);
	end = ((phys_bytes)(s.addr + (u64_t)s.stride * s.height) +
	    AARCH64_PAGE_SIZE - 1) & ~(phys_bytes)(AARCH64_PAGE_SIZE - 1);
	fb.offset = (vir_bytes)(s.addr - base);
	fb.base = (vir_bytes)base;

	/*
	 * The same arrangement as the UART's (serial.c): physical until the
	 * MMU comes on - when that is the address to use - and rewritten by
	 * pre_init_high() to the device mapping after. The range is a no-map
	 * reservation on every machine that has one of these nodes, so the
	 * linear map of RAM has a hole there and the two do not collide
	 * (pre_init.c, scan_reserved()).
	 */
	kern_phys_map_ptr(base, end - base, VMMF_UNCACHED | VMMF_WRITE,
	    &fb_phys_map, (vir_bytes)&fb.base);

	fb.fg = pixel(FG_R, FG_G, FG_B);
	fb.bg = pixel(BG_R, BG_G, BG_B);
	fill_rows(0, fb.rows);
	fb.col = fb.row = 0;
}

/*===========================================================================*
 *				fbcon_owned				     *
 *===========================================================================*/
void
fbcon_owned(int owned)
{
	/*
	 * Given back: start on a cleared line where the driver's picture
	 * leaves off, which is anywhere - the row this console last wrote is
	 * as good as any.
	 */
	if (fb.owned && !owned && fb.format != FB_NONE)
		fill_rows(fb.row, 1);
	fb.owned = owned;
}

/*===========================================================================*
 *				fbcon_putc				     *
 *===========================================================================*/
void
fbcon_putc(char ch)
{
	unsigned char c = (unsigned char)ch;

	if (fb.format == FB_NONE)
		return;

	/*
	 * A driver has the screen and draws the console better (scrolling,
	 * colour, a keyboard).  Except on a panic: the driver may be what
	 * died, and the last words matter more than its picture.
	 */
	if (fb.owned && !kinfo.minix_panicing)	/* set only by panic() */
		return;

	switch (c) {
	case '\n':
		new_line();
		return;
	case '\r':
		fb.col = 0;
		return;
	case '\b':
		if (fb.col > 0)
			fb.col--;
		return;
	case '\t':
		do
			fbcon_putc(' ');
		while (fb.col % 8 != 0);
		return;
	}

	if (c < ' ')
		return;

	if (fb.col == fb.cols)
		new_line();
	draw_char(fb.col++, fb.row, c);
}
