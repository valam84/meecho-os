/*
 * fbart - a piece of the screen, as text: what is on the display, read
 * back through /dev/fb0 (the buffer the controller shows).
 *
 *   fbart [-x X] [-y Y] [-w W] [-h H] [-s STEP] [device]
 *
 * Prints H/STEP lines of W/STEP characters: ' ' where the pixel is the
 * most common colour of the area (the background), '#' where it is bright,
 * '+' where it is something else.  Before them one line with the geometry
 * and the background, after them "FBART ink N": the pixels that are not
 * background, which is what a test counts.  Default: the top left 320x64,
 * every second pixel.
 */
#include <sys/types.h>
#include <sys/ioctl.h>

#include <minix/fb.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
	struct fb_var_screeninfo v;
	struct fb_fix_screeninfo f;
	unsigned x0 = 0, y0 = 0, w = 320, h = 64, step = 2, x, y, n, i;
	uint32_t *row, bg, cand[16];
	unsigned count[16], ncand = 0, best = 0;
	long ink = 0;
	int c, fd;
	const char *dev = "/dev/fb0";

	while ((c = getopt(argc, argv, "x:y:w:h:s:")) != -1) {
		switch (c) {
		case 'x': x0 = (unsigned)atoi(optarg); break;
		case 'y': y0 = (unsigned)atoi(optarg); break;
		case 'w': w = (unsigned)atoi(optarg); break;
		case 'h': h = (unsigned)atoi(optarg); break;
		case 's': step = (unsigned)atoi(optarg); break;
		default:
			fprintf(stderr, "usage: fbart [-x X] [-y Y] [-w W] [-h H] "
			    "[-s STEP] [device]\n");
			return 2;
		}
	}
	if (optind < argc)
		dev = argv[optind];
	if (step == 0)
		step = 1;

	if ((fd = open(dev, O_RDONLY)) < 0 ||
	    ioctl(fd, FBIOGET_VSCREENINFO, &v) != 0 ||
	    ioctl(fd, FBIOGET_FSCREENINFO, &f) != 0) {
		fprintf(stderr, "fbart: %s: %s\n", dev, strerror(errno));
		return 1;
	}
	if (x0 + w > v.xres)
		w = v.xres > x0 ? v.xres - x0 : 0;
	if (y0 + h > v.yres)
		h = v.yres > y0 ? v.yres - y0 : 0;
	if ((row = malloc((size_t)w * 4)) == NULL)
		return 1;

	/* The background: the most common colour of the first row. */
	if (pread(fd, row, (size_t)w * 4, (off_t)y0 * f.line_length +
	    x0 * 4) != (ssize_t)(w * 4))
		return 1;
	for (x = 0; x < w; x++) {
		uint32_t px = row[x] & 0xffffff;

		for (i = 0; i < ncand && cand[i] != px; i++)
			;
		if (i == ncand && ncand < 16) {
			cand[ncand] = px;
			count[ncand++] = 0;
		}
		if (i < ncand)
			count[i]++;
	}
	for (i = 1; i < ncand; i++)
		if (count[i] > count[best])
			best = i;
	bg = ncand ? cand[best] : 0;

	printf("FBART %ux%u stride %u, area %u,%u %ux%u step %u, background "
	    "0x%06x\n", v.xres, v.yres, f.line_length, x0, y0, w, h, step,
	    (unsigned)bg);
	for (y = y0; y < y0 + h; y += step) {
		if (pread(fd, row, (size_t)w * 4, (off_t)y * f.line_length +
		    x0 * 4) != (ssize_t)(w * 4))
			break;
		putchar('|');
		for (x = 0; x < w; x += step) {
			uint32_t px = row[x] & 0xffffff;
			unsigned lum = ((px >> 16) & 0xff) + ((px >> 8) & 0xff) +
			    (px & 0xff);

			if (px == bg)
				putchar(' ');
			else
				putchar(lum > 3 * 0x80 ? '#' : '+');
		}
		puts("|");
		for (n = 0; n < w; n++)
			if ((row[n] & 0xffffff) != bg)
				ink++;
	}
	printf("FBART ink %ld\n", ink);
	return 0;
}
