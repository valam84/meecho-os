/*
 * fbbench - what a whole-screen change costs on the way to the screen:
 * the case of a terminal scrolling or a window moving, where every row
 * differs from the frame before (port/clover/hw-gpu.md, §20).
 *
 * Maps the scanout buffers (FBIOC_MAP), and for each frame fills the next
 * buffer with bands that move by a few rows a frame - so every row
 * changes - and shows it with the whole screen as damage (FBIOC_FLIP).
 * Prints the frames per second, the drawing and the flip apart.
 *
 *   fbbench [-n frames] [device]       default 120, /dev/fb0
 */
#include <sys/types.h>
#include <sys/ioctl.h>

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../../../minix/drivers/video/fb/arch/aarch64/fb_ioc.h"

static double
now(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec / 1e9;
}

int
main(int argc, char **argv)
{
	struct fb_map m;
	struct fb_flip f;
	const char *dev = "/dev/fb0";
	double t0, t1, draw = 0, flip = 0;
	uint32_t *p, x, y, c;
	int fd, n = 120, i, b, opt;

	while ((opt = getopt(argc, argv, "n:")) != -1) {
		if (opt == 'n')
			n = atoi(optarg);
		else {
			fprintf(stderr, "usage: fbbench [-n frames] [device]\n");
			return 2;
		}
	}
	if (optind < argc)
		dev = argv[optind];
	if ((fd = open(dev, O_RDWR)) < 0 || ioctl(fd, FBIOC_MAP, &m) < 0) {
		perror(dev);
		return 1;
	}
	printf("fbbench: %ux%u, stride %u, %u buffers\n", m.width, m.height,
	    m.stride, m.nbuf);
	b = (int)m.shown;
	for (i = 0; i < n; i++) {
		b = (b + 1) % (int)m.nbuf;
		p = (uint32_t *)(uintptr_t)m.addr[b];
		t0 = now();
		for (y = 0; y < m.height; y++) {
			/* bands 32 rows high, moving 7 rows a frame */
			c = ((y + 7u * (unsigned)i) / 32) & 1 ? 0x2a3550 :
			    0x101828;
			c += (uint32_t)(i & 0x1f);
			for (x = 0; x < m.width; x++)
				p[y * (m.stride / 4) + x] = c;
		}
		t1 = now();
		draw += t1 - t0;
		memset(&f, 0, sizeof(f));
		f.buf = (uint32_t)b;
		f.y0 = 0;
		f.y1 = m.height;
		if (ioctl(fd, FBIOC_FLIP, &f) < 0) {
			perror("FBIOC_FLIP");
			return 1;
		}
		flip += now() - t1;
	}
	printf("FBBENCH %d whole-screen frames in %.2f s: %.1f frames/s; "
	    "drawing %.1f ms, flip %.1f ms a frame\n", n, draw + flip,
	    n / (draw + flip), draw * 1000 / n, flip * 1000 / n);
	close(fd);
	return 0;
}
