#ifndef _FB_DISPLAY_H
#define _FB_DISPLAY_H

/*
 * The display of an AArch64 machine, as the frame buffer driver sees it.
 *
 * A picture on a monitor is three parts in a row, and the split of the code
 * follows the parts, the way the rest of this port does (PLAN.md, "Порт на
 * новую плату: границы"):
 *
 *   fb_arch.c   the tree: which parts this machine has, and the order they
 *               are brought up in; the frame buffer memory; the API fb.c
 *               calls
 *   rk_vop2.c   the display controller - Rockchip VOP2 - which reads the
 *               frame buffer and turns it into timed pixels
 *   dw_hdmi.c   the encoder - Synopsys DesignWare HDMI TX with its own PHY -
 *               which turns timed pixels into TMDS on the connector
 *   fb_rk.c     the SoC around both: clocks, the pixel PLL, the power
 *               domain, pins and the GRF bits, none of which belongs to
 *               either part
 *
 * Every number in these files was read off the vendor system running on the
 * board before it was written (port/cb2-display/): Linux brought the same
 * pipeline up at 1280x720 and its registers are the reference each part
 * prints its own against.
 */

#include <minix/drivers.h>
#include <minix/log.h>

#include <stdint.h>

#include <dev/videomode/videomode.h>

extern struct log fb_log;

/* One block of registers: where the tree says it is, and where we see it. */
struct disp_regs {
	phys_bytes	base;
	size_t		size;
	volatile uint8_t *v;		/* NULL until mapped */
};

static inline uint32_t
disp_rd(const struct disp_regs *r, unsigned off)
{
	return *(volatile uint32_t *)(r->v + off);
}

static inline void
disp_wr(const struct disp_regs *r, unsigned off, uint32_t val)
{
	*(volatile uint32_t *)(r->v + off) = val;
}

/*
 * Rockchip's write-mask convention, which every register of the clock unit,
 * the GRF and the PMU follows: the value goes in bits 15:0 and a one in bit
 * n+16 lets bit n change.  So none of those registers is read before it is
 * written, and no other driver's field in the same register can be lost.
 */
#define RK_HIWORD(v, width, shift) \
	((((uint32_t)1 << (width)) - 1) << ((shift) + 16) | \
	 ((uint32_t)(v) << (shift)))

/* Everything the tree said about the pipeline. */
struct disp_info {
	struct disp_regs vop;		/* the display controller */
	struct disp_regs hdmi;		/* the encoder */
	struct disp_regs grf;		/* what both point at */
	struct disp_regs cru;
	struct disp_regs pmucru;
	struct disp_regs pmu;

	const char	*vop_compatible;
	int		 power_domain;	/* -1 when the tree names none */

	/* The encoder's pins, as phandles of pin groups ("pinctrl-0"). */
	uint32_t	 pinctrl[4];
	unsigned	 npinctrl;
};

/*
 * fb_rk.c - the Rockchip SoC around the display.
 *
 * rk_vo_power() switches the VO power domain on, if the firmware left it
 * off.  rk_vo_clocks() opens the gates of both parts and sets the pixel
 * clock; it answers the rate it actually got, in kHz.  rk_vo_pins() muxes
 * the encoder's DDC and CEC pins.  rk_vo_report() prints the registers it
 * owns beside the reference.
 */
int rk_vo_map(struct disp_info *d);
int rk_vo_power(struct disp_info *d);
int rk_vo_clocks(struct disp_info *d, unsigned pixel_khz, unsigned *got_khz);
void rk_vo_pins(struct disp_info *d, const void *dtb);
void rk_vo_report(struct disp_info *d);

/*
 * rk_vop2.c - the display controller.
 *
 * vop2_set_mode() programs one video port, one window and the route to the
 * HDMI encoder, and commits it.  vop2_scanning() is the proof that does not
 * need a monitor: whether the timing generator is running, and on which
 * line it is.
 */
int vop2_init(struct disp_regs *vop);
int vop2_set_mode(const struct videomode *m, phys_bytes fb, unsigned stride);
int vop2_scanning(unsigned *line);
void vop2_report(void);

/*
 * dw_hdmi.c - the encoder and its PHY.
 *
 * dwhdmi_hpd() is whether a monitor is plugged in.  dwhdmi_read_edid()
 * reads one 128-byte block over the encoder's own DDC master.
 * dwhdmi_set_mode() is the whole bring-up for one mode; hdmi_mode zero
 * sends DVI, which every HDMI sink takes.  dwhdmi_force_color() replaces the
 * picture with one colour inside the encoder - a test of everything after
 * the display controller, without the display controller.
 */
int dwhdmi_init(struct disp_regs *hdmi);
int dwhdmi_hpd(void);
int dwhdmi_read_edid(unsigned block, uint8_t *buf);
int dwhdmi_set_mode(const struct videomode *m, int hdmi_mode);
void dwhdmi_force_color(int on, uint8_t r, uint8_t g, uint8_t b);
int dwhdmi_phy_locked(void);
void dwhdmi_report(void);

#endif /* _FB_DISPLAY_H */
