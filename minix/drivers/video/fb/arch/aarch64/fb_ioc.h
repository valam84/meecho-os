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

/*
 * The frame for a compositor (stage 11.4): the scanout buffers, mapped into
 * the caller, and the request that shows one of them.
 *
 * FBIOC_MAP maps every scanout buffer the driver has into the process that
 * asks - the display controller fetches them by physical address, so they
 * are the driver's physically contiguous memory, shared through VM - and
 * makes that process the owner of the screen until its last close of the
 * device.  Buffer 0 is the one the console draws into while nobody owns
 * the screen.  The format is XRGB8888, little-endian (the word is
 * 0x00RRGGBB), DRM's fourcc 'XR24'.
 *
 * FBIOC_FLIP shows buffer "buf": its lines are cleaned out of the
 * processor's caches (the controller reads behind them), and its address
 * goes into the window, taken at the next frame start.  With three buffers
 * the call returns at once, after waiting only for the previous FLIP to have
 * been taken: the buffer shown before stays on the screen until the next
 * frame start, and the one to draw into next is the one shown two FLIPs ago
 * - (shown + 1) % 3, cycling.  With two, it returns once "buf" is on the
 * screen, so the other may be drawn into.  Rows y0..y1-1 only are cleaned
 * if y1 > y0: damage.
 */
#define FB_MAX_BUFFERS	3
#define FB_FORMAT_XRGB8888	0x34325258u	/* fourcc 'XR24' */

struct fb_map {
	uint32_t	width, height;	/* pixels */
	uint32_t	stride;		/* bytes from one row to the next */
	uint32_t	format;		/* FB_FORMAT_* */
	uint32_t	size;		/* bytes of one buffer */
	uint32_t	nbuf;		/* buffers below */
	uint32_t	shown;		/* the one on the screen now */
	uint32_t	_pad;
	uint64_t	addr[FB_MAX_BUFFERS];	/* in the caller */
};

struct fb_flip {
	uint32_t	buf;
	uint32_t	y0, y1;		/* rows drawn since its last showing */
	uint32_t	_pad;
};

#define FBIOC_MAP	_IOR('V', 0x41, struct fb_map)
#define FBIOC_FLIP	_IOW('V', 0x42, struct fb_flip)

/*
 * The mode, changed while a compositor runs.
 *
 * FBIOC_MODES lists the modes there are to choose from - the monitor's own
 * (EDID, its CEA-861 extension included) that this driver makes, or the
 * virtual display's - and which one is on the screen.
 *
 * FBIOC_SETMODE takes one of them: width and height, and the rate nearest
 * to hz (0: nearest to 60).  The scanout buffers do not change - they are
 * made for the largest mode of the list - so a compositor's mapping of them
 * stays good; the geometry does, and comes back here.  The buffer on the
 * screen is cleared to black before the controller and the encoder are set
 * for the new mode, and the next FLIP shows the new picture.  Only the
 * owner of the screen may ask, or anybody while nobody owns it (then the
 * console takes the new size).  A mode not in the list is EINVAL; one the
 * hardware did not take is EIO, with the old mode back on the screen.
 */
#define FB_MAX_MODES	32

struct fb_modeinfo {
	uint32_t	width, height;	/* pixels */
	uint32_t	hz;		/* frames a second, rounded */
	uint32_t	khz;		/* pixel clock */
};

/*
 * A monitor plugged in after bring-up is the driver's to notice (it
 * watches the hot plug line) and to read; the mode is then the driver's
 * to change only while nobody owns the screen.  The owner, a compositor,
 * is told by "generation", which counts the monitors that have come since
 * bring-up: when it moves, "preferred" is the mode the driver would have
 * taken - the monitor's preferred, fbmode=, or the largest it makes - and
 * the compositor asks for it with FBIOC_SETMODE, so that its frames and
 * the screen change size together.
 */
struct fb_modes {
	uint32_t	n;		/* modes below */
	uint32_t	current;	/* the one on the screen; n if none is */
	uint32_t	preferred;	/* the one the driver would choose; n */
	uint32_t	generation;	/* monitors come since bring-up */
	struct fb_modeinfo mode[FB_MAX_MODES];
};

struct fb_setmode {
	uint32_t	width, height;	/* in: asked for; out: taken */
	uint32_t	hz;		/* in: 0 for 60; out: taken */
	uint32_t	stride;		/* out: bytes from one row to the next */
};

typedef char fb_modes_fits[(sizeof(struct fb_modes) <= 4095) ? 1 : -1];

#define FBIOC_MODES	_IOR('V', 0x43, struct fb_modes)
#define FBIOC_SETMODE	_IOWR('V', 0x44, struct fb_setmode)

/*
 * The virtual display only (virtual=1, QEMU), for the stands: a monitor
 * taken out (0), or plugged in - the four-mode one the display starts with
 * (1), or one that has 1024x768 and 800x600 only (2).  What the driver does
 * about it from there is what it does about a real one on the HDMI port:
 * the same tick, the same reading of the list, the same choice.  ENOTTY on
 * real hardware.
 */
#define FB_VPLUG_NONE	0
#define FB_VPLUG_WIDE	1
#define FB_VPLUG_SMALL	2
#define FBIOC_VPLUG	_IOW('V', 0x45, uint32_t)

#endif /* _FB_IOC_H */
