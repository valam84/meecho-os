/*
 * Rockchip VOP2, the display controller of the RK3566/RK3568.
 *
 * It reads a frame buffer and sends timed pixels to an encoder.  Inside are
 * three video ports (timing generators), six windows (fetch units), a mixer
 * chain between them, and a crossbar from ports to encoders.  This file
 * drives the smallest useful part of it: video port 0, one window (Smart0,
 * which has no scaler and no compression - the simplest fetch unit on the
 * chip), the mixer chain set to pass that window through, and the HDMI
 * output fed from port 0.
 *
 * The register map and the order of the bring-up are the vendor kernel's
 * (rockchip_drm_vop2.c, rockchip_vop2_reg.c and rockchip_vop_reg.h of
 * 6.1.115-btt-rockchip64); every value written for 1280x720 equals what that
 * kernel left in the registers on the board, except the frame buffer
 * address, which there is an IOMMU address (port/cb2-display/,
 * vop2-recipe.md has the decode of every word).  Things worth knowing that
 * a register list does not say:
 *
 *   - most registers are double-buffered and take effect at the next frame
 *     start of their port, after the port's bit in REG_CFG_DONE is set.
 *     That bit reads one until the frame start has taken the new values,
 *     so it is also the cheapest proof that the port is running.  Bit 15
 *     of REG_CFG_DONE must be in every write: on this SoC the port bits
 *     have no write mask, and bit 15 is what lets them be written at all;
 *   - releasing a port from standby takes effect at once, so it is done
 *     last, after the timing has been committed;
 *   - on the RK3566 three of the six windows are mirrors without a fetch
 *     unit of their own, and the chip will not display at all until
 *     OTP_WIN_EN says so - the vendor kernel writes it for the RK3566 only,
 *     and it is harmless on the RK3568;
 *   - the controller does not snoop the processor's caches; whoever draws
 *     cleans the cache (fb_arch.c).
 *
 * No reset line is named in the tree and the vendor kernel never resets the
 * block, so neither does this file: it writes every register the picture
 * depends on, rather than trusting what reset or a previous user left.
 */

#include <minix/drivers.h>
#include <minix/log.h>
#include <minix/sysutil.h>

#include <errno.h>
#include <stdint.h>

#include "display.h"

/* SYS */
#define VOP_REG_CFG_DONE	0x000
#define  CFG_DONE_EN		(1u << 15)	/* in every write */
#define  CFG_DONE_VP0		(1u << 0)
#define VOP_VERSION_INFO	0x004
#define VOP_AUTO_GATING_CTRL	0x008
#define VOP_DSP_IF_EN		0x028
#define  IF_EN_HDMI0		(1u << 1)	/* mux 11:10 = 0: from VP0 */
#define VOP_DSP_IF_CTRL		0x02c
#define VOP_DSP_IF_POL		0x030
#define  IF_POL_CFG_IMMEDIATE	(1u << 28)
#define  IF_POL_HDMI_DCLK	(1u << 7)
#define  IF_POL_HDMI_HSYNC_POS	(1u << 4)
#define  IF_POL_HDMI_VSYNC_POS	(1u << 5)
#define VOP_OTP_WIN_EN		0x050
#define VOP_SYS_STATUS0		0x060	/* 31:16 VP0's current line */
#define VOP_VP0_LINE_FLAG	0x070	/* 12:0 and 28:16 */
#define VOP_SYS0_INT_EN		0x080	/* write-masked */
#define VOP_SYS0_INT_CLR	0x084
#define VOP_SYS0_INT_RAW	0x08c
#define  SYS_INT_BUS_ERROR	(1u << 1)
#define VOP_VP0_INT_CLR		0x0a4	/* write-masked */
#define VOP_VP0_INT_RAW		0x0ac
#define  VP_INT_FS		(1u << 0)
#define  VP_INT_LINE_FLAG	(1u << 2)
#define  VP_INT_POST_BUF_EMPTY	(1u << 4)	/* the fetch fell behind */

/* Overlay */
#define VOP_OVL_CTRL		0x600
#define  OVL_CTRL_SEL_IMMEDIATE	(1u << 28)
#define VOP_OVL_LAYER_SEL	0x604
#define VOP_OVL_PORT_SEL	0x608
#define VOP_OVL_MIX(n)		(0x650 + 0x10 * (n))	/* four words each */
#define VOP_HDR0_SRC_COLOR_CTRL	0x6c0
#define VOP_VP0_BG_MIX_CTRL	0x6e0
#define VOP_CLUSTER_DLY_NUM	0x6f0
#define VOP_SMART_DLY_NUM	0x6f8

/* Video port 0 */
#define VP0_DSP_CTRL		0xc00
#define  DSP_CTRL_STANDBY	(1u << 31)
#define  DSP_CTRL_PRE_DITHER_DOWN (1u << 16)
#define  DSP_CTRL_OUT_MODE_AAAA	0xf	/* 30-bit to the encoder */
#define VP0_DUAL_CHANNEL_CTRL	0xc04
#define VP0_COLOR_BAR_CTRL	0xc08
#define VP0_DSP_BG		0xc2c
#define VP0_PRE_SCAN_HTIMING	0xc30
#define VP0_POST_DSP_HACT_INFO	0xc34
#define VP0_POST_DSP_VACT_INFO	0xc38
#define VP0_POST_SCL_FACTOR	0xc3c
#define VP0_POST_SCL_CTRL	0xc40
#define VP0_DSP_HTOTAL_HS_END	0xc48
#define VP0_DSP_HACT_ST_END	0xc4c
#define VP0_DSP_VTOTAL_VS_END	0xc50
#define VP0_DSP_VACT_ST_END	0xc54
#define VP0_BCSH_CTRL		0xc60
#define VP0_BCSH_COLOR_BAR	0xc6c

/* Smart0 */
#define SMART0_CTRL0		0x1c00
#define SMART0_REGION0_CTRL	0x1c10
#define  REGION_EN		(1u << 0)	/* format 5:1 = 0: ARGB8888 */
#define SMART0_REGION0_YRGB_MST	0x1c14
#define SMART0_REGION0_CBR_MST	0x1c18
#define SMART0_REGION0_VIR	0x1c1c
#define SMART0_REGION0_ACT_INFO	0x1c20
#define SMART0_REGION0_DSP_INFO	0x1c24
#define SMART0_REGION0_DSP_ST	0x1c28
#define SMART0_REGION0_SCL_CTRL	0x1c30
#define  SCL_CTRL_BILINEAR	0x44	/* filters; modes "none" */
#define SMART0_REGION0_SCL_FACTOR 0x1c34
#define SMART0_REGION0_SCL_OFFSET 0x1c3c
#define SMART0_COLOR_KEY_CTRL	0x1cd0
#define SMART0_BG_EN		0x1cd4

/* HDR, bypassed */
#define VOP_SDR2HDR_CTRL	0x2010
#define VOP_HDR2SDR_CTRL	0x2020
#define  HDR_BYPASS		0x300

/*
 * The layer map: every window on port 0, Smart0 in layer 0 at the bottom
 * of the chain, the rest above it disabled.  These are the vendor's
 * one-display defaults and the board's values; the mixers between the
 * layers are set to pass the layer below through unchanged.
 */
#define LAYER_SEL_SMART0_FIRST	0x54706213
#define PORT_SEL_ALL_ON_VP0	0x00000755
#define MIX_SRC_COLOR		0x00000020
#define MIX_DST_COLOR		0x00ff0060
#define MIX_SRC_ALPHA		0x00000020
#define MIX_DST_ALPHA		0x00000074
#define NR_MIXERS		5

/*
 * Pipeline delays, in pixels: the background has to wait for the slowest
 * layer path of port 0 (42 for SDR), and Smart0's path takes 20.
 */
#define BG_DLY			42
#define SMART_DLY		0x14141414

static struct disp_regs *regs;

static uint32_t
rd(unsigned off)
{
	return disp_rd(regs, off);
}

static void
wr(unsigned off, uint32_t val)
{
	disp_wr(regs, off, val);
}

int
vop2_init(struct disp_regs *r)
{
	regs = r;
	log_info(&fb_log, "vop2: version 0x%08x\n", rd(VOP_VERSION_INFO));
	return OK;
}

/* Wait for the port to take a commit: one frame, 17 ms at 60 Hz. */
static int
wait_cfg_done(void)
{
	unsigned spins;

	for (spins = 0; spins < 100; spins++) {
		if (!(rd(VOP_REG_CFG_DONE) & CFG_DONE_VP0))
			return OK;
		micro_delay(1000);
	}
	return ETIMEDOUT;
}

int
vop2_set_mode(const struct videomode *m, phys_bytes fb, unsigned stride)
{
	unsigned hsync_len, vsync_len, hact_st, hact_end, vact_st, vact_end;
	uint32_t pol;
	int n;

	if (fb > 0xffffffffUL || (fb + (phys_bytes)stride * m->vdisplay) >
	    0x100000000ULL) {
		/* The window's address register is 32 bits wide. */
		log_warn(&fb_log, "vop2: frame buffer at 0x%lx is out of the "
		    "controller's reach\n", (unsigned long)fb);
		return EINVAL;
	}
	if (m->flags & VID_INTERLACE) {
		log_warn(&fb_log, "vop2: interlaced modes are not done here\n");
		return EINVAL;
	}

	hsync_len = m->hsync_end - m->hsync_start;
	vsync_len = m->vsync_end - m->vsync_start;
	hact_st = m->htotal - m->hsync_start;
	hact_end = hact_st + m->hdisplay;
	vact_st = m->vtotal - m->vsync_start;
	vact_end = vact_st + m->vdisplay;

	/* Global: the RK3566's window enable, commits on, auto-gating off. */
	wr(VOP_OTP_WIN_EN, 1);
	wr(VOP_REG_CFG_DONE, CFG_DONE_EN);
	wr(VOP_AUTO_GATING_CTRL, 0);

	/* The layer map, applied at once rather than at a frame start. */
	wr(VOP_OVL_CTRL, OVL_CTRL_SEL_IMMEDIATE);
	wr(VOP_OVL_PORT_SEL, PORT_SEL_ALL_ON_VP0);
	wr(VOP_OVL_LAYER_SEL, LAYER_SEL_SMART0_FIRST);

	/* Interrupts stay masked; only the raw status is read. */
	wr(VOP_SYS0_INT_CLR, SYS_INT_BUS_ERROR << 16 | SYS_INT_BUS_ERROR);
	wr(VOP_VP0_INT_CLR, 0xffff0000u | 0xffff);

	/* The output: HDMI0 from VP0, with the mode's sync polarities. */
	pol = IF_POL_CFG_IMMEDIATE | IF_POL_HDMI_DCLK;
	if (m->flags & VID_PHSYNC)
		pol |= IF_POL_HDMI_HSYNC_POS;
	if (m->flags & VID_PVSYNC)
		pol |= IF_POL_HDMI_VSYNC_POS;
	wr(VOP_DSP_IF_POL, pol);
	wr(VOP_DSP_IF_EN, IF_EN_HDMI0);
	wr(VOP_DSP_IF_CTRL, 0);

	/* The port, held in standby while it is described. */
	wr(VP0_DSP_CTRL, DSP_CTRL_STANDBY | DSP_CTRL_PRE_DITHER_DOWN |
	    DSP_CTRL_OUT_MODE_AAAA);
	wr(VP0_DUAL_CHANNEL_CTRL, 0);
	wr(VP0_DSP_HTOTAL_HS_END, (uint32_t)m->htotal << 16 | hsync_len);
	wr(VP0_DSP_HACT_ST_END, hact_st << 16 | hact_end);
	wr(VP0_DSP_VACT_ST_END, vact_st << 16 | vact_end);
	wr(VP0_DSP_VTOTAL_VS_END, (uint32_t)m->vtotal << 16 | vsync_len);
	wr(VOP_VP0_LINE_FLAG, (rd(VOP_VP0_LINE_FLAG) & ~0x1fff1fffu) |
	    vact_end << 16 | vact_end);

	/* Post block at 1:1, black background, every enhancement off. */
	wr(VP0_POST_DSP_HACT_INFO, hact_st << 16 | hact_end);
	wr(VP0_POST_DSP_VACT_INFO, vact_st << 16 | vact_end);
	wr(VP0_POST_SCL_FACTOR, 0x10001000);
	wr(VP0_POST_SCL_CTRL, 0);
	wr(VP0_DSP_BG, 0);
	wr(VP0_COLOR_BAR_CTRL, 0);
	wr(VP0_BCSH_CTRL, 0x88);
	wr(VP0_BCSH_COLOR_BAR, 0);
	wr(VOP_SDR2HDR_CTRL, HDR_BYPASS);
	wr(VOP_HDR2SDR_CTRL, HDR_BYPASS);

	/* Mixers pass through; delays line the paths up. */
	for (n = 0; n < NR_MIXERS; n++) {
		wr(VOP_OVL_MIX(n) + 0x0, MIX_SRC_COLOR);
		wr(VOP_OVL_MIX(n) + 0x4, MIX_DST_COLOR);
		wr(VOP_OVL_MIX(n) + 0x8, MIX_SRC_ALPHA);
		wr(VOP_OVL_MIX(n) + 0xc, MIX_DST_ALPHA);
	}
	wr(VOP_HDR0_SRC_COLOR_CTRL, 0);
	wr(VOP_VP0_BG_MIX_CTRL, (uint32_t)BG_DLY << 24);
	wr(VP0_PRE_SCAN_HTIMING,
	    (uint32_t)(BG_DLY + m->hdisplay / 2 - 1) << 16 | hsync_len);
	wr(VOP_CLUSTER_DLY_NUM, 0);
	wr(VOP_SMART_DLY_NUM, SMART_DLY);
	wr(VOP_OVL_CTRL, 0);

	/*
	 * The window: XRGB8888 is the controller's ARGB8888 with the alpha
	 * byte ignored by pass-through mixers; the stride is in 32-bit words.
	 */
	wr(SMART0_REGION0_YRGB_MST, (uint32_t)fb);
	wr(SMART0_REGION0_CBR_MST, 0);
	wr(SMART0_REGION0_VIR, stride / 4);
	wr(SMART0_REGION0_SCL_FACTOR, 0);
	wr(SMART0_REGION0_SCL_CTRL, SCL_CTRL_BILINEAR);
	wr(SMART0_REGION0_SCL_OFFSET, 0);
	wr(SMART0_BG_EN, 0);
	wr(SMART0_COLOR_KEY_CTRL, 0);
	wr(SMART0_REGION0_ACT_INFO, (uint32_t)(m->vdisplay - 1) << 16 |
	    (m->hdisplay - 1));
	wr(SMART0_REGION0_DSP_INFO, (uint32_t)(m->vdisplay - 1) << 16 |
	    (m->hdisplay - 1));
	wr(SMART0_REGION0_DSP_ST, 0);
	wr(SMART0_CTRL0, 0);
	wr(SMART0_REGION0_CTRL, REGION_EN);

	/* Commit, then let the port run. */
	wr(VOP_REG_CFG_DONE, CFG_DONE_EN | CFG_DONE_VP0);
	wr(VP0_DSP_CTRL, DSP_CTRL_PRE_DITHER_DOWN | DSP_CTRL_OUT_MODE_AAAA);

	if (wait_cfg_done() != OK) {
		log_warn(&fb_log, "vop2: port 0 did not take the commit "
		    "(cfg_done 0x%08x) - is dclk running?\n",
		    rd(VOP_REG_CFG_DONE));
		return EIO;
	}
	return OK;
}

/*
 * Until the port has taken what was committed: the commit bit reads one
 * until a frame start has applied the new values.  Polled at a quarter of
 * a millisecond - busily, which is why the callers wait only when they must.
 */
static int
wait_taken(void)
{
	unsigned spins;

	for (spins = 0; spins < 400; spins++) {
		if (!(rd(VOP_REG_CFG_DONE) & CFG_DONE_VP0))
			return OK;
		micro_delay(250);
	}
	return ETIMEDOUT;
}

/*
 * Show another frame buffer of the same mode: a new address for the window,
 * committed, taken at the next frame start - which is when the buffer shown
 * before stops being read.
 *
 * A commit not taken yet is waited for first: an address written over it
 * would be taken in its place, and the buffer it named would never have
 * been on the screen.  Waiting for this commit too ("wait") is for a caller
 * with two buffers, who draws into the one shown before as soon as this
 * returns.  With three there is no need: the next frame goes into the
 * buffer shown two commits ago, and the next call waits for this one.  The
 * wait cost a busy frame per flip - a quarter of a core while the cursor
 * moved, on the board.
 */
int
vop2_scanout(phys_bytes fb, int wait)
{
	if (fb > 0xffffffffUL)
		return EINVAL;
	if (wait_taken() != OK)
		return ETIMEDOUT;
	wr(SMART0_REGION0_YRGB_MST, (uint32_t)fb);
	wr(VOP_REG_CFG_DONE, CFG_DONE_EN | CFG_DONE_VP0);
	return wait ? wait_taken() : OK;
}

/*
 * The window's address as the controller has it, and whether a commit is
 * still waiting for a frame start: the hardware's side of "which buffer is
 * on the screen", for the driver's report.
 */
uint32_t
vop2_window_addr(int *pending)
{
	*pending = (rd(VOP_REG_CFG_DONE) & CFG_DONE_VP0) != 0;
	return rd(SMART0_REGION0_YRGB_MST);
}

/*
 * Is the port scanning?  Three answers that need no monitor: the line
 * counter moves, a frame start comes along within a frame, and the fetch
 * has not fallen behind - the last is what an unreadable frame buffer
 * looks like.  Returns OK with the line last seen, or why not.
 */
int
vop2_scanning(unsigned *line)
{
	unsigned a, b, spins;
	uint32_t raw;

	wr(VOP_VP0_INT_CLR, (VP_INT_FS | VP_INT_POST_BUF_EMPTY) << 16 |
	    VP_INT_FS | VP_INT_POST_BUF_EMPTY);
	wr(VOP_SYS0_INT_CLR, SYS_INT_BUS_ERROR << 16 | SYS_INT_BUS_ERROR);

	a = rd(VOP_SYS_STATUS0) >> 16;
	micro_delay(1000);
	b = rd(VOP_SYS_STATUS0) >> 16;
	*line = b;

	for (spins = 0; spins < 50; spins++) {
		if (rd(VOP_VP0_INT_RAW) & VP_INT_FS)
			break;
		micro_delay(1000);
	}
	raw = rd(VOP_VP0_INT_RAW);

	log_info(&fb_log, "vop2: line %u then %u; frame start %s; raw 0x%03x, "
	    "bus 0x%02x\n", a, b, (raw & VP_INT_FS) ? "seen" : "NOT seen",
	    raw & 0xffff, rd(VOP_SYS0_INT_RAW) & 0xff);

	if (a == b || !(raw & VP_INT_FS))
		return EIO;
	if (raw & VP_INT_POST_BUF_EMPTY) {
		log_warn(&fb_log, "vop2: the window fetch underflowed - the "
		    "frame buffer is not readable to it\n");
		return EFAULT;
	}
	if (rd(VOP_SYS0_INT_RAW) & SYS_INT_BUS_ERROR) {
		log_warn(&fb_log, "vop2: AXI bus error\n");
		return EFAULT;
	}
	return OK;
}

/*
 * The board's Linux at 1280x720, for the registers written above.  The
 * frame buffer address is the one that differs by design: there it is an
 * address in the controller's IOMMU.
 */
static const struct {
	const char *name;
	uint16_t off;
	uint32_t val;
} reference[] = {
	{ "OTP_WIN_EN",		VOP_OTP_WIN_EN,		0x00000001 },
	{ "DSP_IF_EN",		VOP_DSP_IF_EN,		0x00000002 },
	{ "DSP_IF_POL",		VOP_DSP_IF_POL,		0x100000b0 },
	{ "OVL_CTRL",		VOP_OVL_CTRL,		0x00000000 },
	{ "OVL_LAYER_SEL",	VOP_OVL_LAYER_SEL,	0x54706213 },
	{ "OVL_PORT_SEL",	VOP_OVL_PORT_SEL,	0x00000755 },
	{ "MIX0 DST_COLOR",	VOP_OVL_MIX(0) + 4,	0x00ff0060 },
	{ "VP0_BG_MIX_CTRL",	VOP_VP0_BG_MIX_CTRL,	0x2a000000 },
	{ "SMART_DLY_NUM",	VOP_SMART_DLY_NUM,	0x14141414 },
	{ "VP0_LINE_FLAG",	VOP_VP0_LINE_FLAG,	0x62e902e9 },
	{ "VP0_DSP_CTRL",	VP0_DSP_CTRL,		0x0001000f },
	{ "VP0_PRE_SCAN",	VP0_PRE_SCAN_HTIMING,	0x02a90028 },
	{ "VP0_POST_HACT",	VP0_POST_DSP_HACT_INFO,	0x01040604 },
	{ "VP0_POST_VACT",	VP0_POST_DSP_VACT_INFO,	0x001902e9 },
	{ "VP0_POST_SCL",	VP0_POST_SCL_FACTOR,	0x10001000 },
	{ "VP0_HTOTAL_HS_END",	VP0_DSP_HTOTAL_HS_END,	0x06720028 },
	{ "VP0_HACT_ST_END",	VP0_DSP_HACT_ST_END,	0x01040604 },
	{ "VP0_VTOTAL_VS_END",	VP0_DSP_VTOTAL_VS_END,	0x02ee0005 },
	{ "VP0_VACT_ST_END",	VP0_DSP_VACT_ST_END,	0x001902e9 },
	{ "VP0_BCSH_CTRL",	VP0_BCSH_CTRL,		0x00000088 },
	{ "SMART0_REGION0_CTRL", SMART0_REGION0_CTRL,	0x00000001 },
	{ "SMART0_YRGB_MST",	SMART0_REGION0_YRGB_MST, 0x00000000 },
	{ "SMART0_VIR",		SMART0_REGION0_VIR,	0x00000500 },
	{ "SMART0_ACT_INFO",	SMART0_REGION0_ACT_INFO, 0x02cf04ff },
	{ "SMART0_DSP_INFO",	SMART0_REGION0_DSP_INFO, 0x02cf04ff },
	{ "SMART0_SCL_CTRL",	SMART0_REGION0_SCL_CTRL, 0x00000044 },
	{ "SMART0_CTRL1",	SMART0_CTRL0 + 4,	0x0000d0c0 },
	{ "SDR2HDR_CTRL",	VOP_SDR2HDR_CTRL,	0x00000300 },
	{ "HDR2SDR_CTRL",	VOP_HDR2SDR_CTRL,	0x00000300 },
};

void
vop2_report(void)
{
	unsigned i;
	uint32_t v;

	for (i = 0; i < sizeof(reference) / sizeof(reference[0]); i++) {
		v = rd(reference[i].off);
		log_info(&fb_log, "vop2 %-19s 0x%04x = 0x%08x (reference "
		    "0x%08x)%s\n", reference[i].name, reference[i].off, v,
		    reference[i].val, v == reference[i].val ? "" :
		    reference[i].off == SMART0_REGION0_YRGB_MST ?
		    " differs: IOMMU address there" : " DIFFERS");
	}
}
