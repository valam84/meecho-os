/*
 * fbflip - the compositor's path to the screen, without a compositor
 * (stage 11.4): FBIOC_MAP, draw, FBIOC_FLIP, and read the screen back.
 *
 * Maps the frame buffer driver's scanout buffers (VM shared memory over
 * the driver's physically contiguous buffers), draws a different pattern
 * into each buffer it may show, flips to it, and reads the screen back
 * with pread(2), which reads the buffer the display shows: what this
 * process wrote through its mapping must be what the driver scans out.
 * Then it closes the device, and the console's buffer must be back.
 *
 *   fbflip [device]        default /dev/fb0
 *
 * Prints one line per check and "FBFLIP checks N failed F".  Works on the
 * board (VOP2: the flip waits for a frame start) and on QEMU with the
 * driver started with virtual=1 (no controller: the same path above it).
 */
#include <sys/types.h>
#include <sys/ioctl.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../../../minix/drivers/video/fb/arch/aarch64/fb_ioc.h"

static int checks, fails;

static void
check(const char *name, int ok, const char *why)
{
	checks++;
	if (!ok)
		fails++;
	printf("%s  %s%s%s\n", ok ? "ok  " : "FAIL", name, ok ? "" : ": ",
	    ok ? "" : why);
	fflush(stdout);
}

static uint32_t
pattern(int b, uint32_t x, uint32_t y)
{
	return (uint32_t)(0x00102030u * (unsigned)(b + 1)) ^ (x * 7u) ^
	    (y << 8);
}

/* Pixels (x, y) of the screen, through read(2). */
static uint32_t
screen_px(int fd, const struct fb_map *m, uint32_t x, uint32_t y)
{
	uint32_t px = 0xdeadbeef;

	if (pread(fd, &px, sizeof(px), (off_t)y * m->stride + x * 4) !=
	    sizeof(px))
		return 0xdeadbeef;
	return px;
}

int
main(int argc, char **argv)
{
	const char *dev = argc > 1 ? argv[1] : "/dev/fb0";
	struct fb_map m;
	struct fb_flip f;
	struct timespec t0, t1;
	char why[160];
	uint32_t x, y, *p, console0;
	int fd, b, bad, r;

	if ((fd = open(dev, O_RDWR)) < 0) {
		fprintf(stderr, "fbflip: %s: %s\n", dev, strerror(errno));
		return 1;
	}
	memset(&m, 0, sizeof(m));
	r = ioctl(fd, FBIOC_MAP, &m);
	snprintf(why, sizeof(why), "%s", strerror(errno));
	check("FBIOC_MAP", r == 0, why);
	if (r != 0)
		return 1;
	printf("      %ux%u stride %u format 0x%08x, %u buffer(s), %u shown\n",
	    m.width, m.height, m.stride, m.format, m.nbuf, m.shown);
	check("at least two buffers, buffer 0 shown", m.nbuf >= 2 &&
	    m.shown == 0, "");
	console0 = screen_px(fd, &m, 0, 0);

	for (b = 1; b < (int)m.nbuf; b++) {
		p = (uint32_t *)(uintptr_t)m.addr[b];
		for (y = 0; y < m.height; y++)
			for (x = 0; x < m.width; x++)
				p[y * (m.stride / 4) + x] = pattern(b, x, y);
		memset(&f, 0, sizeof(f));
		f.buf = (uint32_t)b;
		clock_gettime(CLOCK_MONOTONIC, &t0);
		r = ioctl(fd, FBIOC_FLIP, &f);
		clock_gettime(CLOCK_MONOTONIC, &t1);
		snprintf(why, sizeof(why), "%s", strerror(errno));
		check(b == 1 ? "FBIOC_FLIP to buffer 1" :
		    "FBIOC_FLIP to buffer 2", r == 0, why);
		printf("      flip took %ld us\n",
		    (long)((t1.tv_sec - t0.tv_sec) * 1000000 +
		    (t1.tv_nsec - t0.tv_nsec) / 1000));

		/* Corners, middle, a scattering: the screen is the drawing. */
		bad = 0;
		for (y = 0; y < m.height; y += 97)
			for (x = 0; x < m.width; x += 131)
				if (screen_px(fd, &m, x, y) != pattern(b, x, y))
					bad++;
		if (screen_px(fd, &m, m.width - 1, m.height - 1) !=
		    pattern(b, m.width - 1, m.height - 1))
			bad++;
		snprintf(why, sizeof(why), "%d pixel(s) differ", bad);
		check("the screen shows what this process drew", bad == 0, why);
	}

	/* Another process cannot take the screen meanwhile. */
	close(fd);
	if ((fd = open(dev, O_RDONLY)) >= 0) {
		snprintf(why, sizeof(why), "pixel 0x%08x, console had 0x%08x",
		    screen_px(fd, &m, 0, 0), console0);
		check("after close: the console's buffer is back",
		    screen_px(fd, &m, 0, 0) == console0, why);
		close(fd);
	}

	printf("FBFLIP checks %d failed %d\n", checks, fails);
	return fails ? 1 : 0;
}
