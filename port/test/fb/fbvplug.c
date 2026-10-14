/*
 * fbvplug - the virtual display's monitor taken out or plugged in, for the
 * stands (port/gfx/test/qemu-mode.py).
 *
 *   fbvplug 0    taken out
 *   fbvplug 1    the four-mode monitor the virtual display starts with
 *   fbvplug 2    one with 1024x768 and 800x600 only
 *
 * The driver (fb with virtual=1) takes the change up on its next ticks as
 * it takes up a monitor on the HDMI port: the line held for a while, the
 * list read, the mode chosen - changed by the driver while nobody owns the
 * screen, by the compositor (which polls) while one does.  FBIOC_VPLUG is
 * ENOTTY on real hardware.
 *
 * Built by the stand with the cross compiler, against the driver's header:
 *   -I minix/drivers/video/fb/arch/aarch64
 */
#include <sys/ioctl.h>

#include <err.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include "fb_ioc.h"

int
main(int argc, char **argv)
{
	uint32_t plug;
	int fd;

	if (argc != 2)
		errx(2, "usage: fbvplug 0|1|2");
	plug = (uint32_t)strtoul(argv[1], NULL, 10);
	if ((fd = open("/dev/fb0", O_RDWR)) < 0)
		err(1, "/dev/fb0");
	if (ioctl(fd, FBIOC_VPLUG, &plug) != 0)
		err(1, "FBIOC_VPLUG %u", plug);
	close(fd);
	return 0;
}
