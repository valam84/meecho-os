/*
 * The one request of the frame buffer driver that is its own on AArch64:
 * hand over the tail of its journal, with the state of the display.
 *
 * Why it exists: on the target board the console is the only place a
 * driver's messages go, and at 1.5 Mbaud through the USB bridge it loses
 * characters in the middle of lines - and a register dump against the
 * reference is exactly the kind of output that cannot be reconstructed from
 * what is left.  So the journal is kept in memory as well and this copies
 * it out, for port/test/fb/fblog.c to print over ssh.  The same shape as
 * the SD driver's SDMMC_IOC_LOG (storage/mmc/sdmmc_ioc.h), for the same
 * reason; the size is bounded by the twelve bits MINIX keeps for the length
 * of an ioctl argument.
 *
 * Asking also re-reads the live state - is a monitor plugged in, is the PHY
 * locked, is the display controller scanning - and adds a line for it, so
 * that "plug the monitor in, then ask" works without a reboot.
 */

#ifndef _FB_IOC_H
#define _FB_IOC_H

#include <sys/ioccom.h>
#include <stdint.h>

struct fb_logdump {
	uint64_t	total;		/* bytes ever written to the journal */
	uint32_t	len;		/* bytes of text below, oldest first */
	char		text[4072];
};

typedef char fb_logdump_fits[(sizeof(struct fb_logdump) <= 4095) ? 1 : -1];

#define FBIOC_LOG	_IOR('V', 0x40, struct fb_logdump)

#endif /* _FB_IOC_H */
