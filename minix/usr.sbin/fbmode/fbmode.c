/*
 * fbmode - the screen's mode, from the console.
 *
 *   fbmode [-d device]                    the modes the screen can be
 *                                         given, the one it has marked
 *   fbmode [-d device] WIDTHxHEIGHT[@HZ]  take one of them
 *
 * The frame buffer driver (fb, AArch64: fb_ioc.h) lists the modes - the
 * monitor's own that it makes, or the virtual display's - and changes the
 * mode on request, the hardware in bring-up's order.  The console on the
 * screen takes the new size when this closes the device: an open puts it
 * aside for the program, and the last close gives it the screen back,
 * drawn at once.
 *
 * While a compositor has the screen the driver refuses anybody else; then
 * the compositor is the one to ask (meechowm -m mode).
 */
#include <sys/ioctl.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "fb_ioc.h"

static void
usage(void)
{
	fprintf(stderr, "usage: fbmode [-d device] [WIDTHxHEIGHT[@HZ]]\n");
	exit(2);
}

int
main(int argc, char **argv)
{
	const char *dev = "/dev/fb0";
	struct fb_modes ms;
	struct fb_setmode s;
	unsigned long w, h = 0, hz = 0;
	char *e;
	uint32_t i;
	int c, fd;

	while ((c = getopt(argc, argv, "d:")) != -1) {
		switch (c) {
		case 'd':
			dev = optarg;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc > 1)
		usage();

	if (argc == 1) {
		w = strtoul(argv[0], &e, 10);
		if (*e == 'x') {
			h = strtoul(e + 1, &e, 10);
			if (*e == '@')
				hz = strtoul(e + 1, &e, 10);
		}
		if (*e != '\0' || w == 0 || h == 0 || w > 8192 || h > 8192 ||
		    hz > 1000)
			errx(2, "%s: not WIDTHxHEIGHT or WIDTHxHEIGHT@HZ",
			    argv[0]);
	}

	if ((fd = open(dev, O_RDWR)) < 0)
		err(1, "%s", dev);

	if (argc == 0) {
		if (ioctl(fd, FBIOC_MODES, &ms) != 0)
			err(1, "%s: the modes", dev);
		for (i = 0; i < ms.n; i++)
			printf("%ux%u@%u%s\n", ms.mode[i].width,
			    ms.mode[i].height, ms.mode[i].hz,
			    i == ms.current ? " current" : "");
		close(fd);
		return 0;
	}

	memset(&s, 0, sizeof(s));
	s.width = (uint32_t)w;
	s.height = (uint32_t)h;
	s.hz = (uint32_t)hz;
	if (ioctl(fd, FBIOC_SETMODE, &s) != 0) {
		if (errno == EPERM)
			errx(1, "%s: a program has the screen; a compositor "
			    "changes it itself (meechowm -m mode %s)", dev,
			    argv[0]);
		if (errno == EINVAL)
			errx(1, "%s: %s is not a mode of the screen (fbmode "
			    "lists them)", dev, argv[0]);
		err(1, "%s: %s", dev, argv[0]);
	}
	printf("%ux%u@%u\n", s.width, s.height, s.hz);
	/* The console has the screen back, in the new size, from here. */
	close(fd);
	return 0;
}
