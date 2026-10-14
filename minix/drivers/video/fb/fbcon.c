/*
 * The console on the screen: the text tty sends to the console line, drawn
 * into the frame buffer.
 *
 * tty does not draw and does not wait for us.  It keeps a copy of all the
 * console line's output in a ring and, once asked (TTY_SCREEN_UP, see
 * <minix/com.h>), gives this driver a read-only grant to it and a notify()
 * whenever it grows.  This file pulls what is new, runs it through a small
 * terminal - enough of VT100 and ANSI colour for what a console sees from
 * the kernel, rc, getty, login and a shell - into a grid of character
 * cells, and draws the cells that changed.  The screen is therefore a
 * second view of the serial console, and starts with as much of the boot
 * as tty's ring still holds.  There is no keyboard: input is the serial
 * line's, or ssh's.
 *
 * Kernel messages - and the printf() of every system service, which goes
 * through the kernel - do not pass through tty on this port: the kernel
 * prints them straight to the UART (do_serial_debug) and never tells tty
 * there is anything to print.  So they come from a second source, the
 * kernel's message buffer, which is mapped into every process through
 * minix_kerninfo exactly as tty itself reads it; it is polled ten times a
 * second and drawn in cyan.  The order between the two streams is right to
 * within that tenth.
 *
 * The grid is the authority and the pixels are drawn from it, so a program
 * that takes the frame buffer over (open /dev/fb0) costs the console
 * nothing: text keeps arriving in the grid while the program draws, and
 * the whole grid is drawn again when the last program closes the device.
 *
 * The font is Spleen 8x16 (Frederic Cambus, BSD 2-clause), imported
 * unchanged from NetBSD into sys/dev/wsfont/; at 1280x720 it makes the
 * 160 by 45 console Linux has on the same screen.
 */

#include <minix/chardriver.h>
#include <minix/drivers.h>
#include <minix/fb.h>
#include <minix/safecopies.h>
#include <minix/sysutil.h>
#include <minix/type.h>
#include <lib.h>			/* get_minix_kerninfo() */

#include <sys/types.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dev/videomode/videomode.h>
#include <dev/videomode/edidvar.h>

#include "fb.h"
#include "fbcon.h"
#include "style.h"

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
#include <dev/wsfont/spleen8x16.h>

#define FONT		spleen8x16
#define CW		8		/* cell width, pixels */
#define CH		16		/* cell height */

/* A cell: the character, then foreground and background colour. */
#define CELL(c, fg, bg)	((uint32_t)(uint8_t)(c) | (uint32_t)(fg) << 8 | \
			 (uint32_t)(bg) << 16)
#define CELL_CHAR(v)	((v) & 0xff)
#define CELL_FG(v)	(((v) >> 8) & 0xff)
#define CELL_BG(v)	(((v) >> 16) & 0xff)

/*
 * The sixteen colours ANSI escapes mean, as MEECHO has them - the ones
 * foot has on the desktop (minix/share/style/palette, made into style.h) -
 * but for black, which on a console is the ground everything stands on:
 * there it is the desktop's ground, ink.  (Dark enough for fbpeek, which
 * takes a pixel with no channel above 0x40 for background.)
 */
static const uint32_t palette[16] = STYLE_ANSI;
#define COLOUR(i)	((i) == DEF_BG ? STYLE_INK : palette[i])
#define DEF_FG		7
#define DEF_BG		0

#define MAX_PARAMS	8

static struct {
	int		attached;	/* tty gave us the ring */
	int		paused;		/* a program has the frame buffer */
	int		minor;

	/* The frame buffer, as the arch part describes it. */
	uint8_t	       *fb;
	unsigned	stride;		/* bytes per line */
	unsigned	width, height;
	uint32_t	pixel[16];	/* the palette in the buffer's format */

	/* The grid. */
	unsigned	cols, rows;
	uint32_t       *cells;
	uint8_t	       *dirty;		/* per row */
	unsigned	scrolled;	/* lines scrolled since the last draw */
	unsigned	cx, cy;		/* the cursor */
	unsigned	drawn_cx, drawn_cy;
	int		wrap_next;	/* VT100's deferred wrap */
	uint8_t		fg, bg;
	int		bold, reverse;

	/* The escape parser. */
	enum { S_TEXT, S_ESC, S_CSI, S_SKIP } state;
	int		params[MAX_PARAMS];
	int		nparams;

	/* tty's ring. */
	cp_grant_id_t	grant;
	size_t		ring_size;
	uint64_t	read;

	/* The kernel's message buffer, and how far into it we are. */
	struct kmessages *kmess;
	int		kmess_next;

	/*
	 * Text arrived and is not drawn yet.  Drawing happens on the driver's
	 * tick, ten times a second, however much text comes: the cost of the
	 * screen is bounded by time rather than by how chatty the console is.
	 */
	int		pending;
	int		kmess_on;

	/* What has gone through, for fbcon_stats(). */
	uint64_t	n_tty, n_kmess, n_lf, n_draw, n_flushed;
	uint64_t	us_draw, us_flush;
} con;

#define KMESS_FG	6		/* cyan */

static void
mark(unsigned row)
{
	if (row < con.rows)
		con.dirty[row] = 1;
}

static void
clear_cells(unsigned from, unsigned to)	/* [from, to) in cell indices */
{
	uint32_t blank = CELL(' ', con.fg, con.bg);
	unsigned i;

	for (i = from; i < to; i++)
		con.cells[i] = blank;
	for (i = from / con.cols; i < con.rows && i * con.cols < to; i++)
		mark(i);
}

static void
scroll_up(void)
{
	memmove(con.cells, con.cells + con.cols,
	    (size_t)(con.rows - 1) * con.cols * sizeof(con.cells[0]));
	memmove(con.dirty, con.dirty + 1, con.rows - 1);
	clear_cells((con.rows - 1) * con.cols, con.rows * con.cols);
	con.scrolled++;
}

static void
line_feed(void)
{
	con.n_lf++;
	if (con.cy + 1 < con.rows)
		con.cy++;
	else
		scroll_up();
}

static void
put_char(uint8_t c)
{
	uint8_t fg = con.fg, bg = con.bg;

	if (con.wrap_next) {
		con.cx = 0;
		line_feed();
		con.wrap_next = 0;
	}
	if (con.bold && fg < 8)
		fg += 8;
	if (con.reverse) {
		uint8_t t = fg;
		fg = bg;
		bg = t;
	}
	con.cells[con.cy * con.cols + con.cx] = CELL(c, fg, bg);
	mark(con.cy);
	if (con.cx + 1 < con.cols)
		con.cx++;
	else
		con.wrap_next = 1;
}

static int
param(int i, int dflt)
{
	return (i < con.nparams && con.params[i] > 0) ? con.params[i] : dflt;
}

static void
sgr(void)
{
	int i, p;

	if (con.nparams == 0)
		con.nparams = 1, con.params[0] = 0;
	for (i = 0; i < con.nparams; i++) {
		p = con.params[i];
		if (p == 0) {
			con.fg = DEF_FG;
			con.bg = DEF_BG;
			con.bold = con.reverse = 0;
		} else if (p == 1)
			con.bold = 1;
		else if (p == 22)
			con.bold = 0;
		else if (p == 7)
			con.reverse = 1;
		else if (p == 27)
			con.reverse = 0;
		else if (p >= 30 && p <= 37)
			con.fg = p - 30;
		else if (p == 39)
			con.fg = DEF_FG;
		else if (p >= 40 && p <= 47)
			con.bg = p - 40;
		else if (p == 49)
			con.bg = DEF_BG;
		else if (p >= 90 && p <= 97)
			con.fg = p - 90 + 8;
		else if (p >= 100 && p <= 107)
			con.bg = p - 100 + 8;
	}
}

static void
csi(uint8_t final)
{
	unsigned n, at;

	con.wrap_next = 0;
	switch (final) {
	case 'A':
		n = param(0, 1);
		con.cy = (n > con.cy) ? 0 : con.cy - n;
		break;
	case 'B':
		n = param(0, 1);
		con.cy = (con.cy + n >= con.rows) ? con.rows - 1 : con.cy + n;
		break;
	case 'C':
		n = param(0, 1);
		con.cx = (con.cx + n >= con.cols) ? con.cols - 1 : con.cx + n;
		break;
	case 'D':
		n = param(0, 1);
		con.cx = (n > con.cx) ? 0 : con.cx - n;
		break;
	case 'G':
		n = param(0, 1);
		con.cx = (n > con.cols) ? con.cols - 1 : n - 1;
		break;
	case 'H':
	case 'f':
		n = param(0, 1);
		con.cy = (n > con.rows) ? con.rows - 1 : n - 1;
		n = param(1, 1);
		con.cx = (n > con.cols) ? con.cols - 1 : n - 1;
		break;
	case 'J':
		at = con.cy * con.cols + con.cx;
		switch (param(0, 0)) {
		case 0: clear_cells(at, con.rows * con.cols); break;
		case 1: clear_cells(0, at + 1); break;
		default: clear_cells(0, con.rows * con.cols); break;
		}
		break;
	case 'K':
		at = con.cy * con.cols;
		switch (param(0, 0)) {
		case 0: clear_cells(at + con.cx, at + con.cols); break;
		case 1: clear_cells(at, at + con.cx + 1); break;
		default: clear_cells(at, at + con.cols); break;
		}
		break;
	case 'm':
		sgr();
		break;
	default:
		break;			/* modes, scroll regions: not here */
	}
}

static void
feed(const uint8_t *s, size_t n)
{
	uint8_t c;

	if (n > 0)
		con.pending = 1;
	while (n-- > 0) {
		c = *s++;
		switch (con.state) {
		case S_ESC:
			if (c == '[') {
				con.state = S_CSI;
				con.nparams = 0;
				memset(con.params, 0, sizeof(con.params));
			} else if (c == 'c') {
				con.fg = DEF_FG;
				con.bg = DEF_BG;
				con.bold = con.reverse = 0;
				clear_cells(0, con.rows * con.cols);
				con.cx = con.cy = 0;
				con.state = S_TEXT;
			} else if (c == '(' || c == ')' || c == '#')
				con.state = S_SKIP;	/* one more byte */
			else
				con.state = S_TEXT;
			continue;
		case S_SKIP:
			con.state = S_TEXT;
			continue;
		case S_CSI:
			if (c >= '0' && c <= '9') {
				if (con.nparams == 0)
					con.nparams = 1;
				if (con.nparams <= MAX_PARAMS)
					con.params[con.nparams - 1] =
					    con.params[con.nparams - 1] * 10 +
					    (c - '0');
			} else if (c == ';') {
				if (con.nparams == 0)
					con.nparams = 1;
				con.nparams++;
			} else if (c >= 0x40 && c <= 0x7e) {
				if (con.nparams > MAX_PARAMS)
					con.nparams = MAX_PARAMS;
				csi(c);
				con.state = S_TEXT;
			}		/* '?', '>', spaces: ignored */
			continue;
		case S_TEXT:
			break;
		}

		switch (c) {
		case '\033':
			con.state = S_ESC;
			break;
		case '\r':
			con.cx = 0;
			con.wrap_next = 0;
			break;
		case '\n':
		case '\v':
		case '\f':
			line_feed();
			con.wrap_next = 0;
			break;
		case '\b':
			if (con.cx > 0)
				con.cx--;
			con.wrap_next = 0;
			break;
		case '\t':
			do
				put_char(' ');
			while (con.cx % 8 != 0 && !con.wrap_next);
			break;
		case '\0':
		case '\a':
		case 0x7f:
			break;
		default:
			if (c >= 0x20)
				put_char(c);
			break;
		}
	}
}

/* Draw one row of cells. */
static void
draw_row(unsigned row)
{
	const uint8_t *glyphs = FONT.data, *g;
	unsigned col, y, x;
	uint32_t v, fg, bg, *p;
	uint8_t c, bits;
	int cursor;

	for (col = 0; col < con.cols; col++) {
		v = con.cells[row * con.cols + col];
		c = CELL_CHAR(v);
		fg = con.pixel[CELL_FG(v) & 15];
		bg = con.pixel[CELL_BG(v) & 15];
		cursor = (row == con.cy && col == con.cx);
		if (cursor) {
			uint32_t t = fg;
			fg = bg;
			bg = t;
		}
		if (c < FONT.firstchar ||
		    c >= FONT.firstchar + FONT.numchars)
			c = '?';
		g = glyphs + (size_t)(c - FONT.firstchar) * CH * FONT.stride;
		for (y = 0; y < CH; y++) {
			p = (uint32_t *)(con.fb + (size_t)(row * CH + y) *
			    con.stride) + col * CW;
			bits = g[y];
			for (x = 0; x < CW; x++)
				p[x] = (bits & (0x80 >> x)) ? fg : bg;
		}
	}
}

/* Bring the pixels up to the grid. */
static void
draw(int all)
{
	unsigned row, lo = ~0u, hi = 0, shift;
	u64_t t0, t1, t2;

	if (con.paused)
		return;
	con.pending = 0;
	read_frclock_64(&t0);

	/* Where the cursor was drawn - moved up by any scroll since - and is. */
	if (con.drawn_cy >= con.scrolled)
		mark(con.drawn_cy - con.scrolled);
	mark(con.cy);

	if (all || con.scrolled >= con.rows) {
		memset(con.dirty, 1, con.rows);
	} else if (con.scrolled > 0) {
		/* Move what is already drawn, rather than drawing it again. */
		shift = con.scrolled * CH;
		memmove(con.fb, con.fb + (size_t)shift * con.stride,
		    (size_t)(con.rows * CH - shift) * con.stride);
		lo = 0;
		hi = con.rows - 1;
	}
	con.scrolled = 0;

	for (row = 0; row < con.rows; row++) {
		if (!con.dirty[row])
			continue;
		draw_row(row);
		con.dirty[row] = 0;
		if (row < lo)
			lo = row;
		if (row > hi)
			hi = row;
	}
	con.drawn_cx = con.cx;
	con.drawn_cy = con.cy;

	con.n_draw++;
	read_frclock_64(&t1);
	if (lo <= hi) {
		arch_fb_flush(con.minor, (size_t)lo * CH * con.stride,
		    (size_t)(hi - lo + 1) * CH * con.stride);
		con.n_flushed += (uint64_t)(hi - lo + 1) * CH * con.stride;
	}
	read_frclock_64(&t2);
	con.us_draw += frclock_64_to_micros(delta_frclock_64(t0, t1));
	con.us_flush += frclock_64_to_micros(delta_frclock_64(t1, t2));
}

/*
 * The console's pixels: the buffer on the screen now - which is not always
 * the same one (a compositor shows its own scanout buffers, and gives the
 * first back when it is done) - cleared, and all of the console on it.
 */
static void
take_screen(void)
{
	struct device dev;

	if (arch_get_device(con.minor, &dev) != OK)
		return;
	con.fb = (uint8_t *)(vir_bytes)dev.dv_base;
	memset(con.fb, 0, (size_t)con.height * con.stride);
	arch_fb_flush(con.minor, 0, (size_t)con.height * con.stride);
	draw(1);
}

/*
 * Set the grid up for the frame buffer and ask tty for the console's text.
 * Without tty's answer (an older tty, or none) there is no console, and the
 * frame buffer is the programs' alone.
 */
int
fbcon_attach(int minor, int kmess)
{
	struct fb_var_screeninfo v;
	struct device dev;
	message m;
	unsigned i;
	int r;

	if (con.attached)
		return OK;
	if (arch_get_varscreeninfo(minor, &v) != OK || v.bits_per_pixel != 32 ||
	    arch_get_device(minor, &dev) != OK)
		return ENXIO;

	con.minor = minor;
	con.fb = (uint8_t *)(vir_bytes)dev.dv_base;
	con.stride = v.xres_virtual * 4;
	con.width = v.xres;
	con.height = v.yres;
	con.cols = v.xres / CW;
	con.rows = v.yres / CH;
	for (i = 0; i < 16; i++)
		con.pixel[i] = (COLOUR(i) >> 16 & 0xff) << v.red.offset |
		    (COLOUR(i) >> 8 & 0xff) << v.green.offset |
		    (COLOUR(i) & 0xff) << v.blue.offset;
	if ((con.cells = calloc(con.cols * con.rows, sizeof(con.cells[0]))) ==
	    NULL || (con.dirty = calloc(con.rows, 1)) == NULL)
		return ENOMEM;
	con.fg = DEF_FG;
	con.bg = DEF_BG;
	clear_cells(0, con.cols * con.rows);

	memset(&m, 0, sizeof(m));
	m.m_type = TTY_SCREEN_UP;
	if ((r = ipc_sendrec(TTY_PROC_NR, &m)) != OK || m.m_type != OK) {
		printf("fb: tty gives no console text (%d, %d); "
		    "no console on the screen\n", r, m.m_type);
		return r != OK ? r : m.m_type;
	}
	con.grant = (cp_grant_id_t)m.m_u32.data[0];
	con.ring_size = m.m_u32.data[1];
	con.read = 0;
	con.attached = 1;

	/*
	 * Kernel messages from here on.  Their past is not taken: it cannot
	 * be put in order with tty's, and half of it is this very boot's
	 * driver chatter that the serial log has anyway.
	 */
	con.kmess_on = kmess;
	if ((con.kmess = get_minix_kerninfo()->kmessages) != NULL)
		con.kmess_next = con.kmess->km_next;
	printf("fb: console on the screen, %ux%u characters\n", con.cols,
	    con.rows);

	/*
	 * Clear the test card away, then take what tty has so far.  Not if a
	 * program has the screen already - a compositor started before the
	 * card's time was up: its frame stays, and the console waits for the
	 * screen to be given back, as it does when the program comes later.
	 */
	if (!con.paused)
		take_screen();
	fbcon_pull();
	return OK;
}

/* tty says there is more: take it. */
void
fbcon_pull(void)
{
	static uint8_t buf[4096];
	uint64_t written, again;
	size_t off, n;

	if (!con.attached)
		return;

	for (;;) {
		if (sys_safecopyfrom(TTY_PROC_NR, con.grant, 0,
		    (vir_bytes)&written, sizeof(written)) != OK)
			return;
		if (written - con.read > con.ring_size)
			con.read = written - con.ring_size;	/* missed */
		if (con.read == written)
			break;
		off = con.read % con.ring_size;
		n = written - con.read;
		if (n > con.ring_size - off)
			n = con.ring_size - off;
		if (n > sizeof(buf))
			n = sizeof(buf);
		if (sys_safecopyfrom(TTY_PROC_NR, con.grant,
		    sizeof(uint64_t) + off, (vir_bytes)buf, n) != OK)
			return;
		/* Overwritten while we copied it?  Then it is not that text. */
		if (sys_safecopyfrom(TTY_PROC_NR, con.grant, 0,
		    (vir_bytes)&again, sizeof(again)) != OK)
			return;
		if (again - con.read > con.ring_size)
			continue;
		feed(buf, n);
		con.read += n;
		con.n_tty += n;
	}
	/* Drawn on the next tick (fbcon_tick). */
}

/*
 * The kernel's messages since the last look.  They are drawn in their own
 * colour, on a line of their own, and the tty stream's colours and parser
 * state are put back afterwards - it may have been in the middle of an
 * escape sequence when the kernel spoke.
 */
void
fbcon_kmess(void)
{
	struct kmessages *km = con.kmess;
	int saved_params[MAX_PARAMS], saved_nparams, saved_state;
	uint8_t fg, bg, c;
	int bold, reverse, next;

	if (!con.attached || km == NULL || (next = km->km_next) ==
	    con.kmess_next || next < 0 || next >= _KMESS_BUF_SIZE)
		return;

	fg = con.fg;
	bg = con.bg;
	bold = con.bold;
	reverse = con.reverse;
	saved_state = con.state;
	saved_nparams = con.nparams;
	memcpy(saved_params, con.params, sizeof(saved_params));

	con.fg = KMESS_FG;
	con.bg = DEF_BG;
	con.bold = con.reverse = 0;
	con.state = S_TEXT;
	if (con.cx != 0 || con.wrap_next) {
		con.cx = 0;
		con.wrap_next = 0;
		line_feed();
	}
	while (con.kmess_next != next) {
		c = (uint8_t)km->km_buf[con.kmess_next];
		con.kmess_next = (con.kmess_next + 1) % _KMESS_BUF_SIZE;
		con.n_kmess++;
		if (c == '\n') {		/* the kernel's lines have no CR */
			con.cx = 0;
			con.wrap_next = 0;
			line_feed();
		} else
			feed(&c, 1);
	}

	con.fg = fg;
	con.bg = bg;
	con.bold = bold;
	con.reverse = reverse;
	con.state = saved_state;
	con.nparams = saved_nparams;
	memcpy(con.params, saved_params, sizeof(saved_params));
}

/*
 * The driver's tick, ten times a second: the kernel's new messages, then
 * whatever arrived since the last tick, drawn once.
 */
void
fbcon_tick(void)
{
	if (!con.attached)
		return;
	if (con.kmess_on)
		fbcon_kmess();
	if (con.pending)
		draw(0);
}

/* One line about what the console has done, for the driver's journal. */
void
fbcon_stats(char *buf, size_t len)
{
	snprintf(buf, len, "console %s%s: %ux%u, cursor %u,%u; %llu bytes from "
	    "tty, %llu from the kernel (at %d of its buffer), %llu line feeds, "
	    "%llu draws taking %llu ms, %llu MB flushed taking %llu ms",
	    con.attached ? "attached" : "not attached",
	    con.paused ? ", paused" : "", con.cols, con.rows, con.cx, con.cy,
	    (unsigned long long)con.n_tty, (unsigned long long)con.n_kmess,
	    con.kmess_next, (unsigned long long)con.n_lf,
	    (unsigned long long)con.n_draw,
	    (unsigned long long)(con.us_draw / 1000),
	    (unsigned long long)(con.n_flushed >> 20),
	    (unsigned long long)(con.us_flush / 1000));
}

/*
 * The mode changed (FBIOC_SETMODE): a grid for the new size.  What fits is
 * kept - the rows up to the cursor, as many as the new grid has, and their
 * left columns - so the prompt is still there; and if the console has the
 * screen, all of it is drawn at once, in the new geometry.  Without memory
 * for the new grid the console lets go of the screen for good: the old
 * grid in the new geometry would put rows where they no longer belong.
 */
void
fbcon_resize(void)
{
	struct fb_var_screeninfo v;
	unsigned cols, rows, keep, first, r, c;
	uint32_t *cells;
	uint8_t *dirty;

	if (!con.attached || arch_get_varscreeninfo(con.minor, &v) != OK)
		return;
	cols = v.xres / CW;
	rows = v.yres / CH;
	cells = calloc((size_t)cols * rows, sizeof(cells[0]));
	dirty = calloc(rows, 1);
	if (cells == NULL || dirty == NULL || cols == 0 || rows == 0) {
		free(cells);
		free(dirty);
		con.attached = 0;
		printf("fb: no memory for a %ux%u console; no console on the "
		    "screen\n", cols, rows);
		return;
	}
	for (r = 0; r < (size_t)cols * rows; r++)
		cells[r] = CELL(' ', DEF_FG, DEF_BG);
	keep = con.cy + 1 < rows ? con.cy + 1 : rows;
	first = con.cy + 1 - keep;
	for (r = 0; r < keep; r++)
		for (c = 0; c < cols && c < con.cols; c++)
			cells[r * cols + c] = con.cells[(first + r) * con.cols + c];

	free(con.cells);
	free(con.dirty);
	con.cells = cells;
	con.dirty = dirty;
	con.cy -= first;
	if (con.cx >= cols)
		con.cx = cols - 1;
	con.cols = cols;
	con.rows = rows;
	con.stride = v.xres_virtual * 4;
	con.width = v.xres;
	con.height = v.yres;
	con.scrolled = 0;
	con.drawn_cx = con.cx;
	con.drawn_cy = con.cy;
	if (!con.paused)
		take_screen();
}

/* A program opened or closed the frame buffer. */
void
fbcon_pause(int paused)
{
	con.paused = paused;
	if (!paused && con.attached)
		take_screen();
}
