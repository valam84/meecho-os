/*
 * mouseev - what the input server hands to a reader of a mouse device.
 *
 * The other side of the input server, as the compositor will see it: one
 * struct input_event per event, from /dev/mousemux (every mouse) or
 * /dev/mouseN (one).  Each event is printed on a line of its own, and a
 * summary at the end - how far X and Y went, how many presses and
 * releases of each button - which is what a test compares.
 *
 *   mouseev [-t seconds] [-n events] [-q] [device]
 *
 * -t stops after that many seconds (default 10), -n after that many
 * events, -q prints only the summary.  The summary line is
 *
 *   MOUSE events N x DX y DY wheel DW b1 P/R b2 P/R b3 P/R
 *
 * Built for the machine: port/board-cc.sh port/test/usbmouse/mouseev.c,
 * or by qemu-inject.py, which puts it into the QEMU disk.
 */
#include <sys/types.h>
#include <minix/input.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t done;

static void
alarm_fired(int sig)
{
	(void)sig;
	done = 1;
}

int
main(int argc, char **argv)
{
	const char *dev = "/dev/mousemux";
	struct input_event ev[16];
	long secs = 10, max = 0, n = 0;
	long x = 0, y = 0, w = 0, press[4] = { 0 }, rel[4] = { 0 };
	struct sigaction sa;
	int c, fd, quiet = 0;
	ssize_t r;
	size_t i;

	while ((c = getopt(argc, argv, "t:n:q")) != -1) {
		switch (c) {
		case 't': secs = strtol(optarg, NULL, 10); break;
		case 'n': max = strtol(optarg, NULL, 10); break;
		case 'q': quiet = 1; break;
		default:
			fprintf(stderr, "usage: mouseev [-t s] [-n events] [-q] "
			    "[device]\n");
			return 2;
		}
	}
	if (optind < argc)
		dev = argv[optind];

	if ((fd = open(dev, O_RDONLY)) < 0) {
		fprintf(stderr, "mouseev: %s: %s\n", dev, strerror(errno));
		return 1;
	}

	/* No SA_RESTART: the alarm has to end a read that waits. */
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = alarm_fired;
	sigaction(SIGALRM, &sa, NULL);
	if (secs > 0)
		alarm((unsigned)secs);

	printf("MOUSE reading %s\n", dev);
	fflush(stdout);

	while (!done && (max == 0 || n < max)) {
		r = read(fd, ev, sizeof(ev));
		if (r < 0) {
			if (errno == EINTR)
				continue;
			fprintf(stderr, "mouseev: read: %s\n", strerror(errno));
			break;
		}
		for (i = 0; i < (size_t)r / sizeof(ev[0]); i++, n++) {
			struct input_event *e = &ev[i];

			if (e->page == INPUT_PAGE_GD && e->code == INPUT_GD_X)
				x += e->value;
			else if (e->page == INPUT_PAGE_GD &&
			    e->code == INPUT_GD_Y)
				y += e->value;
			else if (e->page == INPUT_PAGE_GD &&
			    e->code == INPUT_GD_WHEEL)
				w += e->value;
			else if (e->page == INPUT_PAGE_BUTTON &&
			    e->code >= 1 && e->code <= 3) {
				if (e->value)
					press[e->code]++;
				else
					rel[e->code]++;
			}
			if (!quiet)
				printf("ev page 0x%x code 0x%x value %d flags "
				    "0x%x dev %u\n", e->page, e->code,
				    (int)e->value, e->flags, e->devid);
		}
		fflush(stdout);
	}

	printf("MOUSE events %ld x %ld y %ld wheel %ld b1 %ld/%ld b2 %ld/%ld "
	    "b3 %ld/%ld\n", n, x, y, w, press[1], rel[1], press[2], rel[2],
	    press[3], rel[3]);
	return 0;
}
