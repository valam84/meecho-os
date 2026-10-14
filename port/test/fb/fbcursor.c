/*
 * fbcursor - the cursor window of the frame buffer driver, without a
 * compositor (stage 11.7): FBIOC_CURSOR_MAP, a picture, FBIOC_CURSOR, and
 * the screen read back.
 *
 * Takes the screen (FBIOC_MAP) and shows a frame of one colour, maps the
 * cursor's buffers and draws a 32x32 picture into one of them - an opaque
 * red top half, a half-transparent white quarter, a transparent quarter,
 * premultiplied - and shows it, moves it across the edges of the screen
 * and off it, changes it, hides it.  After every step it reads the screen
 * with pread(2): read(2) answers what is on the screen, the cursor blended
 * in (Cd = Cs + (1 - As) Cd), so the bytes are the check.  Then the other
 * side: a process that is not the owner is refused, bad requests are
 * refused, and closing the device takes the cursor away.
 *
 *   fbcursor [-k] [device]     default /dev/fb0
 *
 * -k: leave the cursor on the screen at (100, 50) and wait for a newline -
 * on the board, for a person at the monitor (and fblog meanwhile).
 *
 * Prints one line per check and "FBCURSOR checks N failed F".  On QEMU
 * with the driver at virtual=1 the screen exists in the reads only; on the
 * board the controller lays the window over the frame, and the reads say
 * what it was told.
 */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/wait.h>

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../../minix/drivers/video/fb/arch/aarch64/fb_ioc.h"

#define BG	0x00336699u		/* the frame */
#define RED	0xffff0000u		/* opaque */
#define HALF	0x80808080u		/* white, half covering, premultiplied */
#define GREEN	0xff00ff00u

static int checks, fails;
static int fd;
static struct fb_map m;
static struct fb_cursor_map cm;

static void
check(const char *name, int ok, const char *fmt, ...)
{
	char why[160];
	va_list ap;

	checks++;
	if (!ok)
		fails++;
	why[0] = '\0';
	if (!ok && fmt != NULL) {
		va_start(ap, fmt);
		vsnprintf(why, sizeof(why), fmt, ap);
		va_end(ap);
	}
	printf("%s  %s%s%s\n", ok ? "ok  " : "FAIL", name, ok ? "" : ": ", why);
	fflush(stdout);
}

/* What the controller makes of picture pixel s over frame pixel d. */
static uint32_t
blend(uint32_t s, uint32_t d)
{
	uint32_t a = 255 - (s >> 24), out = 0, v;
	int sh;

	for (sh = 0; sh < 24; sh += 8) {
		v = ((s >> sh) & 0xff) + (((d >> sh) & 0xff) * a + 127) / 255;
		out |= (v > 255 ? 255 : v) << sh;
	}
	return out | (d & 0xff000000u);
}

static uint32_t
screen_px(uint32_t x, uint32_t y)
{
	uint32_t px = 0xdeadbeef;

	if (pread(fd, &px, sizeof(px), (off_t)y * m.stride + x * 4) !=
	    sizeof(px))
		return 0xdeadbeef;
	return px;
}

/* The 32x32 test picture, at (x, y) of itself. */
static uint32_t
picture(int x, int y)
{
	if (y < 16)
		return RED;
	return x < 16 ? HALF : 0;
}

static void
draw_picture(int b)
{
	uint32_t *p = (uint32_t *)(uintptr_t)cm.addr[b];
	int x, y;

	for (y = 0; y < 32; y++)
		for (x = 0; x < 32; x++)
			p[y * (cm.stride / 4) + x] = picture(x, y);
}

static int
cursor(uint32_t flags, uint32_t buf, uint32_t w, uint32_t h, int x, int y)
{
	struct fb_cursor c;

	memset(&c, 0, sizeof(c));
	c.flags = flags;
	c.buf = buf;
	c.w = w;
	c.h = h;
	c.x = x;
	c.y = y;
	return ioctl(fd, FBIOC_CURSOR, &c) == 0 ? 0 : errno;
}

/* Every pixel of a w x h box at (bx, by) on the screen against want(). */
static void
expect_box(const char *name, int bx, int by, int w, int h,
	uint32_t (*want)(int, int))
{
	uint32_t got, exp;
	int x, y, bad = 0, fx = 0, fy = 0;
	uint32_t fgot = 0, fexp = 0;

	for (y = by; y < by + h; y++)
		for (x = bx; x < bx + w; x++) {
			if (x < 0 || y < 0 || x >= (int)m.width ||
			    y >= (int)m.height)
				continue;
			got = screen_px((uint32_t)x, (uint32_t)y);
			exp = want(x, y);
			if (got != exp && bad++ == 0) {
				fx = x;
				fy = y;
				fgot = got;
				fexp = exp;
			}
		}
	check(name, bad == 0, "%d pixel(s) differ, first at %d,%d: 0x%08x, "
	    "not 0x%08x", bad, fx, fy, fgot, fexp);
}

/* The screen with the 32x32 picture at (cx, cy), or none (cx = -1000). */
static int cx, cy;

static uint32_t
want_picture(int x, int y)
{
	int px = x - cx, py = y - cy;

	if (px < 0 || py < 0 || px >= 32 || py >= 32)
		return BG;
	return blend(picture(px, py), BG);
}

static int gx, gy;

static uint32_t
want_green(int x, int y)
{
	int px = x - gx, py = y - gy;

	if (px < 0 || py < 0 || px >= 8 || py >= 8)
		return BG;
	return blend(GREEN, BG);
}

static uint32_t
want_bg(int x, int y)
{
	(void)x;
	(void)y;
	return BG;
}

/* One read across the cursor's row, starting inside it. */
static void
expect_span(const char *name, int x0, int y, int n)
{
	uint32_t row[64];
	int i, bad = 0;

	if (n > 64)
		n = 64;
	if (pread(fd, row, (size_t)n * 4, (off_t)y * m.stride + x0 * 4) !=
	    (ssize_t)(n * 4)) {
		check(name, 0, "pread: %s", strerror(errno));
		return;
	}
	for (i = 0; i < n; i++)
		if (row[i] != want_picture(x0 + i, y))
			bad++;
	check(name, bad == 0, "%d of %d pixels differ", bad, n);
}

static void
not_owner(void)
{
	struct fb_cursor_map xm;
	pid_t pid;
	int st, f, r1, r2;

	if ((pid = fork()) == 0) {
		if ((f = open("/dev/fb0", O_RDWR)) < 0)
			_exit(9);
		r1 = ioctl(f, FBIOC_CURSOR_MAP, &xm) == 0 ? 0 : errno;
		fd = f;
		r2 = cursor(FB_CURSOR_SHOW, 0, 8, 8, 0, 0);
		_exit(r1 == EPERM && r2 == EPERM ? 0 : 1);
	}
	st = -1;
	(void)waitpid(pid, &st, 0);
	check("a process that is not the owner is refused (EPERM)",
	    WIFEXITED(st) && WEXITSTATUS(st) == 0, "child status 0x%x", st);
}

int
main(int argc, char **argv)
{
	const char *dev = "/dev/fb0";
	uint32_t *b, next;
	uint32_t i;
	int keep = 0, r, f2;
	char line[8];

	if (argc > 1 && strcmp(argv[1], "-k") == 0) {
		keep = 1;
		argc--;
		argv++;
	}
	if (argc > 1)
		dev = argv[1];
	if ((fd = open(dev, O_RDWR)) < 0 || ioctl(fd, FBIOC_MAP, &m) != 0) {
		printf("FAIL  %s: %s\n", dev, strerror(errno));
		return 1;
	}

	/* A frame of one colour on the screen. */
	next = (m.shown + 1) % m.nbuf;
	b = (uint32_t *)(uintptr_t)m.addr[next];
	for (i = 0; i < m.height * (m.stride / 4); i++)
		b[i] = BG;
	{
		struct fb_flip fl;

		memset(&fl, 0, sizeof(fl));
		fl.buf = next;
		check("a frame of one colour", ioctl(fd, FBIOC_FLIP, &fl) == 0,
		    "FBIOC_FLIP: %s", strerror(errno));
	}

	r = ioctl(fd, FBIOC_CURSOR_MAP, &cm) == 0 ? 0 : errno;
	check("FBIOC_CURSOR_MAP", r == 0, "%s", strerror(r));
	if (r != 0)
		goto out;
	check("64x64 ARGB8888, 256 bytes a row, two buffers",
	    cm.max_w == 64 && cm.max_h == 64 && cm.stride == 256 &&
	    cm.format == FB_FORMAT_ARGB8888 && cm.nbuf == 2,
	    "%ux%u, stride %u, format 0x%08x, %u buffer(s)", cm.max_w,
	    cm.max_h, cm.stride, cm.format, cm.nbuf);

	/* The picture, shown. */
	draw_picture(0);
	cx = 100;
	cy = 50;
	r = cursor(FB_CURSOR_SHOW | FB_CURSOR_IMAGE, 0, 32, 32, cx, cy);
	check("shown at 100,50", r == 0, "%s", strerror(r));
	expect_box("opaque, half, transparent and around it", cx - 4, cy - 4,
	    40, 40, want_picture);
	expect_span("a read that starts inside the cursor", cx + 10, cy + 20,
	    40);
	expect_span("a read that ends inside it", cx - 20, cy + 3, 30);

	/* Across the top-left corner, then the bottom-right, then off. */
	cx = -10;
	cy = -12;
	r = cursor(FB_CURSOR_SHOW, 0, 32, 32, cx, cy);
	check("moved to -10,-12", r == 0, "%s", strerror(r));
	expect_box("cut by the top and left edges", 0, 0, 30, 30,
	    want_picture);
	expect_box("and gone from 100,50", 96, 46, 40, 40, want_picture);
	cx = (int)m.width - 5;
	cy = (int)m.height - 5;
	r = cursor(FB_CURSOR_SHOW, 0, 32, 32, cx, cy);
	expect_box("cut by the bottom and right edges", cx - 4, cy - 4, 9, 9,
	    want_picture);
	cx = (int)m.width + 10;
	cy = 0;
	r = cursor(FB_CURSOR_SHOW, 0, 32, 32, cx, cy);
	expect_box("off the screen: nothing", (int)m.width - 8, 0, 8, 8,
	    want_picture);

	/* Another picture, into the other buffer. */
	b = (uint32_t *)(uintptr_t)cm.addr[1];
	for (i = 0; i < 8 * (cm.stride / 4); i++)
		b[i] = GREEN;
	gx = 200;
	gy = 200;
	r = cursor(FB_CURSOR_SHOW | FB_CURSOR_IMAGE, 1, 8, 8, gx, gy);
	check("another picture, buffer 1", r == 0, "%s", strerror(r));
	expect_box("an 8x8 green square at 200,200", 196, 196, 16, 16,
	    want_green);

	/* What is refused. */
	check("65 wide: EINVAL", cursor(FB_CURSOR_SHOW, 0, 65, 8, 0, 0) ==
	    EINVAL, NULL);
	check("buffer 2: EINVAL", cursor(FB_CURSOR_SHOW, 2, 8, 8, 0, 0) ==
	    EINVAL, NULL);
	check("an unknown flag: EINVAL", cursor(0x80 | FB_CURSOR_SHOW, 0, 8,
	    8, 0, 0) == EINVAL, NULL);
	expect_box("and the cursor stays as it was", 196, 196, 16, 16,
	    want_green);
	not_owner();

	/* Hidden. */
	r = cursor(0, 0, 0, 0, 0, 0);
	check("hidden", r == 0, "%s", strerror(r));
	expect_box("the frame alone", 196, 196, 16, 16, want_bg);

	if (keep) {
		draw_picture(0);
		cx = 100;
		cy = 50;
		(void)cursor(FB_CURSOR_SHOW | FB_CURSOR_IMAGE, 0, 32, 32, cx,
		    cy);
		printf("FBCURSOR the picture is at 100,50: Enter to go on\n");
		fflush(stdout);
		(void)fgets(line, sizeof(line), stdin);
	}

	/* Shown again, then the owner closes: the cursor goes with it. */
	gx = 200;
	gy = 200;
	(void)cursor(FB_CURSOR_SHOW, 1, 8, 8, gx, gy);
	close(fd);
	if ((f2 = open(dev, O_RDONLY)) >= 0) {
		uint32_t *con = (uint32_t *)(uintptr_t)m.addr[0];
		int x, y, bad = 0;

		fd = f2;
		for (y = 196; y < 212; y++)
			for (x = 196; x < 212; x++)
				if (screen_px(x, y) !=
				    con[y * (m.stride / 4) + x])
					bad++;
		check("closed: the console's frame, no cursor in it", bad == 0,
		    "%d pixel(s) differ from the console's buffer", bad);
		close(f2);
	}
out:
	printf("FBCURSOR checks %d failed %d\n", checks, fails);
	return fails != 0;
}
