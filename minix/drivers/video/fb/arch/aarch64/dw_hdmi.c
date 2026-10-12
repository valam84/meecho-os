/*
 * Synopsys DesignWare HDMI TX, with the Synopsys PHY behind it.
 *
 * The encoder takes timed pixels from the display controller and puts TMDS
 * on the connector.  It is the same block on many SoCs - Rockchip, i.MX,
 * Allwinner, Amlogic - and this file is only its registers; the clocks and
 * pins around it are fb_rk.c's.  The register numbers and the order of the
 * bring-up are those of the vendor kernel running on the board
 * (drivers/gpu/drm/bridge/synopsys/dw-hdmi.c of 6.1.115-btt-rockchip64) and
 * of NetBSD's dw_hdmi.c; the values were checked against what that kernel
 * left in the registers at 1280x720 (port/cb2-display/ref-720p-forced.txt).
 *
 * Three things about this part that decide the shape of the file:
 *
 *   - the registers are bytes, but on this SoC each sits in a 32-bit slot
 *     ("reg-io-width = <4>"), so register n is at offset 4n;
 *   - the PHY is not memory-mapped at all.  It hangs off an I2C master
 *     inside the encoder, at address 0x69, and every PHY register is a
 *     16-bit write through three byte registers and a "go";
 *   - the monitor's EDID comes over a second I2C master, also inside the
 *     encoder, wired to the connector's DDC pins.
 *
 * What is sent is DVI unless asked otherwise: no data islands, so no
 * InfoFrames and no audio, and RGB at full range with the colour space
 * converter bypassed.  Every HDMI sink must take DVI, and it is the subset
 * with the fewest registers that have to be right at once.  Linux sends
 * HDMI with limited-range RGB at this mode, which is why its CSC is in the
 * path and ours is not (see dwhdmi_report()).
 */

#include <minix/drivers.h>
#include <minix/log.h>
#include <minix/sysutil.h>

#include <errno.h>
#include <stdint.h>

#include "display.h"

/* Identification. */
#define HDMI_DESIGN_ID		0x0000
#define HDMI_REVISION_ID	0x0001
#define HDMI_CONFIG0_ID		0x0004
#define HDMI_CONFIG2_ID		0x0006	/* the PHY type */
#define  PHY_TYPE_HDMI20_TX	0xf3	/* "DWC HDMI 2.0 TX PHY" */

/* Interrupt handler: status, and the mutes that keep it quiet. */
#define HDMI_IH_FC_STAT2	0x0102
#define HDMI_IH_PHY_STAT0	0x0104
#define HDMI_IH_I2CM_STAT0	0x0105
#define  IH_I2CM_DONE		0x02
#define  IH_I2CM_ERROR		0x01
#define HDMI_IH_I2CMPHY_STAT0	0x0108
#define  IH_I2CMPHY_DONE	0x02
#define  IH_I2CMPHY_ERROR	0x01
#define HDMI_IH_MUTE_FC_STAT0	0x0180	/* ...through 0x0189 */
#define HDMI_IH_MUTE_LAST	0x0189
#define HDMI_IH_MUTE		0x01ff
#define  IH_MUTE_WAKEUP		0x02
#define  IH_MUTE_ALL		0x01

/* Video sampler: what the display controller hands over. */
#define HDMI_TX_INVID0		0x0200
#define  TX_INVID0_RGB888	0x01
#define HDMI_TX_INSTUFFING	0x0201
#define  TX_INSTUFFING_ALL	0x07
#define HDMI_TX_GYDATA0		0x0202	/* ...through BCBDATA1, 0x0207 */

/* Video packetizer. */
#define HDMI_VP_PR_CD		0x0801
#define HDMI_VP_STUFF		0x0802
#define HDMI_VP_REMAP		0x0803
#define HDMI_VP_CONF		0x0804
#define  VP_CONF_BYPASS_EN	0x40
#define  VP_CONF_BYPASS_SELECT_VP 0x04
#define  VP_CONF_OUTPUT_BYPASS	0x03
#define HDMI_VP_MASK		0x0807

/* Frame composer. */
#define HDMI_FC_INVIDCONF	0x1000
#define  FC_INVIDCONF_HDCP_KEEPOUT 0x80
#define  FC_INVIDCONF_VSYNC_HIGH 0x40
#define  FC_INVIDCONF_HSYNC_HIGH 0x20
#define  FC_INVIDCONF_DE_HIGH	0x10
#define  FC_INVIDCONF_HDMI	0x08	/* clear: DVI */
#define HDMI_FC_INHACTV0	0x1001
#define HDMI_FC_INHACTV1	0x1002
#define HDMI_FC_INHBLANK0	0x1003
#define HDMI_FC_INHBLANK1	0x1004
#define HDMI_FC_INVACTV0	0x1005
#define HDMI_FC_INVACTV1	0x1006
#define HDMI_FC_INVBLANK	0x1007
#define HDMI_FC_HSYNCINDELAY0	0x1008
#define HDMI_FC_HSYNCINDELAY1	0x1009
#define HDMI_FC_HSYNCINWIDTH0	0x100a
#define HDMI_FC_HSYNCINWIDTH1	0x100b
#define HDMI_FC_VSYNCINDELAY	0x100c
#define HDMI_FC_VSYNCINWIDTH	0x100d
#define HDMI_FC_CTRLDUR		0x1011
#define HDMI_FC_EXCTRLDUR	0x1012
#define HDMI_FC_EXCTRLSPAC	0x1013
#define HDMI_FC_CH0PREAM	0x1014
#define HDMI_FC_CH1PREAM	0x1015
#define HDMI_FC_CH2PREAM	0x1016
#define HDMI_FC_DATAUTO3	0x10b7
#define  FC_DATAUTO3_GCP_AUTO	0x04
#define HDMI_FC_MASK0		0x10d2
#define HDMI_FC_MASK1		0x10d6
#define HDMI_FC_MASK2		0x10da
#define HDMI_FC_PRCONF		0x10e0
#define HDMI_FC_SCRAMBLER_CTRL	0x10e1
#define HDMI_FC_DBGFORCE	0x1200
#define  FC_DBGFORCE_VIDEO	0x01
#define HDMI_FC_DBGTMDS0	0x1219	/* blue (or Cb) */
#define HDMI_FC_DBGTMDS1	0x121a	/* green (or Y) */
#define HDMI_FC_DBGTMDS2	0x121b	/* red (or Cr) */

/* The PHY: its control bits, and the I2C master that reaches the rest. */
#define HDMI_PHY_CONF0		0x3000
#define  PHY_CONF0_SVSRET	0x20
#define  PHY_CONF0_PDDQ		0x10
#define  PHY_CONF0_TXPWRON	0x08
#define  PHY_CONF0_ENHPDRXSENSE	0x04
#define  PHY_CONF0_SELDATAENPOL	0x02
#define  PHY_CONF0_SELDIPIF	0x01
#define HDMI_PHY_TST0		0x3001
#define  PHY_TST0_TSTCLR	0x20
#define HDMI_PHY_STAT0		0x3004
#define  PHY_STAT0_HPD		0x02
#define  PHY_STAT0_TX_PHY_LOCK	0x01
#define HDMI_PHY_MASK0		0x3006
#define HDMI_PHY_I2CM_SLAVE	0x3020
#define  PHY_I2C_ADDR_GEN2	0x69
#define HDMI_PHY_I2CM_ADDRESS	0x3021
#define HDMI_PHY_I2CM_DATAO_1	0x3022
#define HDMI_PHY_I2CM_DATAO_0	0x3023
#define HDMI_PHY_I2CM_DATAI_1	0x3024
#define HDMI_PHY_I2CM_DATAI_0	0x3025
#define HDMI_PHY_I2CM_OPERATION	0x3026
#define  PHY_I2CM_OP_WRITE	0x10
#define  PHY_I2CM_OP_READ	0x01
#define HDMI_PHY_I2CM_INT	0x3027
#define HDMI_PHY_I2CM_CTLINT	0x3028

/* Main controller: clocks, resets and the data path. */
#define HDMI_MC_CLKDIS		0x4001
#define  MC_CLKDIS_HDCP		0x40
#define  MC_CLKDIS_CEC		0x20
#define  MC_CLKDIS_CSC		0x10
#define  MC_CLKDIS_AUD		0x08
#define  MC_CLKDIS_PREP		0x04
#define  MC_CLKDIS_TMDS		0x02
#define  MC_CLKDIS_PIXEL	0x01
#define HDMI_MC_SWRSTZ		0x4002
#define  MC_SWRSTZ_TMDS		0x02
#define HDMI_MC_FLOWCTRL	0x4004
#define  MC_FLOWCTRL_CSC_BYPASS	0x00
#define HDMI_MC_PHYRSTZ		0x4005
#define  MC_PHYRSTZ_ASSERT	0x01	/* active high on the gen2 PHY */
#define HDMI_MC_HEACPHY_RST	0x4007
#define  MC_HEACPHY_RST_ASSERT	0x01
#define HDMI_CSC_CFG		0x4100

/* HDCP block: two of its bits are about plain video, not about HDCP. */
#define HDMI_A_HDCPCFG0		0x5000
#define  A_HDCPCFG0_HDMI	0x01
#define HDMI_A_VIDPOLCFG	0x5009
#define  A_VIDPOLCFG_DE_HIGH	0x10
#define  A_VIDPOLCFG_VSYNC_HIGH	0x08
#define  A_VIDPOLCFG_HSYNC_HIGH	0x02
#define  A_VIDPOLCFG_MASK	0x1a

/* The DDC master. */
#define HDMI_I2CM_SLAVE		0x7e00
#define  DDC_ADDR		0x50
#define  DDC_SEGMENT_ADDR	0x30
#define HDMI_I2CM_ADDRESS	0x7e01
#define HDMI_I2CM_DATAI		0x7e03
#define HDMI_I2CM_OPERATION	0x7e04
#define  I2CM_OP_RD		0x01
#define  I2CM_OP_RD_EXT		0x02
#define HDMI_I2CM_INT		0x7e05
#define  I2CM_INT_DONE_POL	0x08
#define HDMI_I2CM_CTLINT	0x7e06
#define  I2CM_CTLINT_POLS	0x88
#define HDMI_I2CM_DIV		0x7e07
#define  I2CM_DIV_FAST		0x08
#define HDMI_I2CM_SEGADDR	0x7e08
#define HDMI_I2CM_SOFTRSTZ	0x7e09
#define HDMI_I2CM_SEGPTR	0x7e0a
#define HDMI_I2CM_SS_SCL_HCNT_1	0x7e0b
#define HDMI_I2CM_SS_SCL_HCNT_0	0x7e0c
#define HDMI_I2CM_SS_SCL_LCNT_1	0x7e0d
#define HDMI_I2CM_SS_SCL_LCNT_0	0x7e0e

/* PHY registers, behind the I2C master. */
#define PHY_CPCE_CTRL		0x06
#define PHY_CURRCTRL		0x10
#define PHY_PLLPHBYCTRL		0x13
#define PHY_GMPCTRL		0x15
#define PHY_MSM_CTRL		0x17
#define  MSM_CTRL_CKO_SEL_FB_CLK 0x0006
#define PHY_CKSYMTXCTRL		0x09
#define PHY_VLEVCTRL		0x0e
#define PHY_TXTERM		0x19

/*
 * The PHY settings by pixel clock, 8 bits per colour: the first line whose
 * clock is at or above the mode's applies.
 *
 * These are Rockchip's numbers for this PHY (dw_hdmi-rockchip.c of the
 * vendor kernel: rockchip_mpll_cfg, rockchip_cur_ctr, rockchip_phy_config)
 * rather than Synopsys' generic ones - the current control is zero at every
 * rate, which the generic table does not say - and the 74.25 MHz line is the
 * one the board was seen running with: CPCE 0x0072, GMP 0x0001, CURR 0,
 * CKSYM 0x8009, TERM 0x0004, VLEV 0x0272, read back over the PHY's own I2C.
 */
static const struct {
	uint32_t khz;
	uint16_t cpce, gmp;
} mpll_cfg[] = {
	{  30666, 0x00b3, 0x0000 },
	{  36800, 0x00b3, 0x0000 },
	{  46000, 0x00b3, 0x0000 },
	{  61333, 0x0072, 0x0001 },
	{  73600, 0x0072, 0x0001 },
	{  92000, 0x0072, 0x0001 },
	{ 122666, 0x0051, 0x0002 },
	{ 147200, 0x0051, 0x0002 },
	{ 184000, 0x0051, 0x0002 },
	{ 226666, 0x0040, 0x0003 },
	{ 272000, 0x0040, 0x0003 },
	{ 340000, 0x0040, 0x0003 },
};

static const struct {
	uint32_t khz;
	uint16_t sym, term, vlev;
} phy_cfg[] = {
	{  74250, 0x8009, 0x0004, 0x0272 },
	{ 165000, 0x802b, 0x0004, 0x0209 },
	{ 297000, 0x8039, 0x0005, 0x028d },
};

#define NELEM(a)	(sizeof(a) / sizeof((a)[0]))

/*
 * Above 340 MHz HDMI 2.0 scrambles, and that needs the sink's SCDC over
 * DDC; this driver does not do it, so it refuses the mode instead of sending
 * a signal the monitor cannot lock to.
 */
#define DWHDMI_MAX_KHZ	340000

static struct disp_regs *regs;
static uint8_t phy_type;

static uint8_t
rd(unsigned reg)
{
	return disp_rd(regs, reg * 4) & 0xff;
}

static void
wr(unsigned reg, uint8_t val)
{
	disp_wr(regs, reg * 4, val);
}

static void
modb(unsigned reg, uint8_t mask, uint8_t val)
{
	wr(reg, (rd(reg) & ~mask) | (val & mask));
}

/* A write to a PHY register, through the encoder's I2C master. */
static int
phy_wait(void)
{
	unsigned spins;
	uint8_t st;

	for (spins = 0; spins < 1000; spins++) {
		st = rd(HDMI_IH_I2CMPHY_STAT0);
		if (st & (IH_I2CMPHY_DONE | IH_I2CMPHY_ERROR)) {
			wr(HDMI_IH_I2CMPHY_STAT0, st);
			return (st & IH_I2CMPHY_ERROR) ? EIO : OK;
		}
		micro_delay(10);
	}
	return ETIMEDOUT;
}

static int
phy_write(uint8_t reg, uint16_t val)
{
	int r;

	wr(HDMI_IH_I2CMPHY_STAT0, IH_I2CMPHY_DONE | IH_I2CMPHY_ERROR);
	wr(HDMI_PHY_I2CM_ADDRESS, reg);
	wr(HDMI_PHY_I2CM_DATAO_1, val >> 8);
	wr(HDMI_PHY_I2CM_DATAO_0, val & 0xff);
	wr(HDMI_PHY_I2CM_OPERATION, PHY_I2CM_OP_WRITE);
	if ((r = phy_wait()) != OK)
		log_warn(&fb_log, "hdmi: PHY register 0x%02x write: %d\n",
		    reg, r);
	return r;
}

static int
phy_read(uint8_t reg, uint16_t *val)
{
	int r;

	wr(HDMI_IH_I2CMPHY_STAT0, IH_I2CMPHY_DONE | IH_I2CMPHY_ERROR);
	wr(HDMI_PHY_I2CM_ADDRESS, reg);
	wr(HDMI_PHY_I2CM_OPERATION, PHY_I2CM_OP_READ);
	if ((r = phy_wait()) != OK)
		return r;
	*val = (uint16_t)(rd(HDMI_PHY_I2CM_DATAI_1) << 8) |
	    rd(HDMI_PHY_I2CM_DATAI_0);
	return OK;
}

int
dwhdmi_init(struct disp_regs *r)
{
	unsigned reg, version;
	uint8_t mute;

	regs = r;

	/* 0x21 and 0x1a on the board: version 2.11a, as Linux says. */
	version = rd(HDMI_DESIGN_ID) << 8 | rd(HDMI_REVISION_ID);
	phy_type = rd(HDMI_CONFIG2_ID);
	log_info(&fb_log, "hdmi: DesignWare HDMI TX %x.%02xa, PHY type 0x%02x\n",
	    version >> 12, (version >> 4) & 0xff, phy_type);
	if (phy_type != PHY_TYPE_HDMI20_TX)
		log_warn(&fb_log, "hdmi: the PHY settings here are for the "
		    "HDMI 2.0 TX PHY (0x%02x); this one may not take them\n",
		    PHY_TYPE_HDMI20_TX);

	/*
	 * Everything muted: this driver asks the status registers when it
	 * wants to know, and an interrupt nobody takes is only noise.
	 */
	mute = rd(HDMI_IH_MUTE) | IH_MUTE_WAKEUP | IH_MUTE_ALL;
	wr(HDMI_IH_MUTE, mute);
	wr(HDMI_VP_MASK, 0xff);
	wr(HDMI_FC_MASK0, 0xff);
	wr(HDMI_FC_MASK1, 0xff);
	wr(HDMI_FC_MASK2, 0xff);
	wr(HDMI_PHY_MASK0, 0xff);
	wr(HDMI_PHY_I2CM_INT, 0xff);
	wr(HDMI_PHY_I2CM_CTLINT, 0xff);
	wr(HDMI_I2CM_INT, 0xff);
	wr(HDMI_I2CM_CTLINT, 0xff);
	for (reg = HDMI_IH_MUTE_FC_STAT0; reg <= HDMI_IH_MUTE_LAST; reg++)
		wr(reg, 0xff);

	/*
	 * Except the "done" of the two I2C masters.  The mask bit in a
	 * master's own INT register does not only keep the interrupt line
	 * quiet: a masked "done" never reaches IH_I2CM*_STAT0 at all, and
	 * that status is the only way to know a transfer ended.  The first
	 * run on the board masked it with the rest and every PHY write
	 * timed out; the values here are the vendor kernel's and the
	 * board's (0x08 and 0x88: done, NACK and arbitration unmasked, with
	 * their polarities).  The interrupts stay muted in IH_MUTE_*.
	 */
	wr(HDMI_PHY_I2CM_INT, I2CM_INT_DONE_POL);
	wr(HDMI_PHY_I2CM_CTLINT, I2CM_CTLINT_POLS);

	/*
	 * Hot plug and receiver sense are inputs of the PHY, and the PHY
	 * reports them only when told to look.
	 */
	modb(HDMI_PHY_CONF0, PHY_CONF0_ENHPDRXSENSE, PHY_CONF0_ENHPDRXSENSE);

	/*
	 * The DDC master: standard mode, 100 kHz.  The counts are in cycles
	 * of the encoder's 24 MHz register clock, for the high and low times
	 * the vendor kernel uses (4708 and 4916 ns), and are what the board
	 * holds: 0x71 and 0x76.
	 */
	wr(HDMI_I2CM_SOFTRSTZ, 0);
	modb(HDMI_I2CM_DIV, I2CM_DIV_FAST, 0);
	wr(HDMI_I2CM_SS_SCL_HCNT_1, 0);
	wr(HDMI_I2CM_SS_SCL_HCNT_0, 0x71);
	wr(HDMI_I2CM_SS_SCL_LCNT_1, 0);
	wr(HDMI_I2CM_SS_SCL_LCNT_0, 0x76);
	wr(HDMI_I2CM_INT, I2CM_INT_DONE_POL);
	wr(HDMI_I2CM_CTLINT, I2CM_CTLINT_POLS);
	wr(HDMI_IH_I2CM_STAT0, IH_I2CM_DONE | IH_I2CM_ERROR);

	return OK;
}

int
dwhdmi_hpd(void)
{
	return (rd(HDMI_PHY_STAT0) & PHY_STAT0_HPD) != 0;
}

int
dwhdmi_phy_locked(void)
{
	return (rd(HDMI_PHY_STAT0) & PHY_STAT0_TX_PHY_LOCK) != 0;
}

/*
 * One 128-byte EDID block.  Blocks two and up are behind the E-DDC segment
 * pointer, two blocks to a segment; the master sends the pointer itself when
 * asked for an "extended" read.
 */
int
dwhdmi_read_edid(unsigned block, uint8_t *buf)
{
	unsigned n, spins;
	uint8_t st, op;

	wr(HDMI_I2CM_SLAVE, DDC_ADDR);
	wr(HDMI_I2CM_SEGADDR, DDC_SEGMENT_ADDR);
	wr(HDMI_I2CM_SEGPTR, block >> 1);
	op = (block >> 1) ? I2CM_OP_RD_EXT : I2CM_OP_RD;

	for (n = 0; n < 128; n++) {
		wr(HDMI_IH_I2CM_STAT0, IH_I2CM_DONE | IH_I2CM_ERROR);
		wr(HDMI_I2CM_ADDRESS, (block & 1) * 128 + n);
		wr(HDMI_I2CM_OPERATION, op);
		for (spins = 0; spins < 1000; spins++) {
			st = rd(HDMI_IH_I2CM_STAT0);
			if (st & (IH_I2CM_DONE | IH_I2CM_ERROR))
				break;
			micro_delay(10);
		}
		if (!(st & IH_I2CM_DONE) || (st & IH_I2CM_ERROR)) {
			wr(HDMI_IH_I2CM_STAT0, st);
			return (st & IH_I2CM_ERROR) ? EIO : ETIMEDOUT;
		}
		buf[n] = rd(HDMI_I2CM_DATAI);
	}
	wr(HDMI_IH_I2CM_STAT0, IH_I2CM_DONE | IH_I2CM_ERROR);
	return OK;
}

/*
 * The PHY: off, reset, programmed for the pixel clock, on, and locked.
 * The order is the vendor kernel's (hdmi_phy_configure()).
 */
static int
phy_configure(unsigned khz)
{
	unsigned i, m, p;

	for (m = 0; m < NELEM(mpll_cfg) - 1 && mpll_cfg[m].khz < khz; m++)
		;
	for (p = 0; p < NELEM(phy_cfg) - 1 && phy_cfg[p].khz < khz; p++)
		;

	modb(HDMI_PHY_CONF0, PHY_CONF0_SELDATAENPOL | PHY_CONF0_SELDIPIF,
	    PHY_CONF0_SELDATAENPOL);

	/* Off: transmitter down, wait for the PLL to let go, then PDDQ. */
	modb(HDMI_PHY_CONF0, PHY_CONF0_TXPWRON, 0);
	for (i = 0; i < 5 && dwhdmi_phy_locked(); i++)
		micro_delay(2000);
	modb(HDMI_PHY_CONF0, PHY_CONF0_PDDQ, PHY_CONF0_PDDQ);

	/* Out of the low-power state, and a reset pulse. */
	modb(HDMI_PHY_CONF0, PHY_CONF0_SVSRET, PHY_CONF0_SVSRET);
	wr(HDMI_MC_PHYRSTZ, MC_PHYRSTZ_ASSERT);
	wr(HDMI_MC_PHYRSTZ, 0);
	wr(HDMI_MC_HEACPHY_RST, MC_HEACPHY_RST_ASSERT);

	/* Point the I2C master at the PHY. */
	modb(HDMI_PHY_TST0, PHY_TST0_TSTCLR, PHY_TST0_TSTCLR);
	wr(HDMI_PHY_I2CM_SLAVE, PHY_I2C_ADDR_GEN2);
	modb(HDMI_PHY_TST0, PHY_TST0_TSTCLR, 0);

	if (phy_write(PHY_CPCE_CTRL, mpll_cfg[m].cpce) != OK)
		return EIO;
	phy_write(PHY_GMPCTRL, mpll_cfg[m].gmp);
	phy_write(PHY_CURRCTRL, 0);
	phy_write(PHY_PLLPHBYCTRL, 0);
	phy_write(PHY_MSM_CTRL, MSM_CTRL_CKO_SEL_FB_CLK);
	phy_write(PHY_TXTERM, phy_cfg[p].term);
	phy_write(PHY_CKSYMTXCTRL, phy_cfg[p].sym);
	phy_write(PHY_VLEVCTRL, phy_cfg[p].vlev);

	/* On, and wait for the PLL. */
	modb(HDMI_PHY_CONF0, PHY_CONF0_TXPWRON, PHY_CONF0_TXPWRON);
	modb(HDMI_PHY_CONF0, PHY_CONF0_PDDQ, 0);
	for (i = 0; i < 5 && !dwhdmi_phy_locked(); i++)
		micro_delay(2000);
	if (!dwhdmi_phy_locked()) {
		log_warn(&fb_log, "hdmi: the PHY PLL did not lock at %u kHz "
		    "(stat0 0x%02x)\n", khz, rd(HDMI_PHY_STAT0));
		return EIO;
	}
	return OK;
}

int
dwhdmi_set_mode(const struct videomode *m, int hdmi_mode)
{
	unsigned hblank, vblank, hdelay, hwidth, vdelay, vwidth;
	uint8_t conf, clkdis, pol;
	int r;

	if (m->dot_clock > DWHDMI_MAX_KHZ) {
		log_warn(&fb_log, "hdmi: %d kHz needs scrambling, which this "
		    "driver does not do\n", m->dot_clock);
		return EINVAL;
	}

	hblank = m->htotal - m->hdisplay;
	vblank = m->vtotal - m->vdisplay;
	hdelay = m->hsync_start - m->hdisplay;
	hwidth = m->hsync_end - m->hsync_start;
	vdelay = m->vsync_start - m->vdisplay;
	vwidth = m->vsync_end - m->vsync_start;

	/* Black until told otherwise, if the debug colour is ever forced. */
	wr(HDMI_FC_DBGTMDS0, 0);
	wr(HDMI_FC_DBGTMDS1, 0);
	wr(HDMI_FC_DBGTMDS2, 0);

	/*
	 * The frame composer: the timing it should expect.  HDCP keep-out
	 * is set as the vendor kernel sets it - some sinks want 58 pixels
	 * between video and data islands, and the bit buys them.
	 */
	conf = FC_INVIDCONF_HDCP_KEEPOUT | FC_INVIDCONF_DE_HIGH;
	if (m->flags & VID_PVSYNC)
		conf |= FC_INVIDCONF_VSYNC_HIGH;
	if (m->flags & VID_PHSYNC)
		conf |= FC_INVIDCONF_HSYNC_HIGH;
	if (hdmi_mode)
		conf |= FC_INVIDCONF_HDMI;
	wr(HDMI_FC_INVIDCONF, conf);
	wr(HDMI_FC_SCRAMBLER_CTRL, 0);

	wr(HDMI_FC_INHACTV1, m->hdisplay >> 8);
	wr(HDMI_FC_INHACTV0, m->hdisplay & 0xff);
	wr(HDMI_FC_INVACTV1, m->vdisplay >> 8);
	wr(HDMI_FC_INVACTV0, m->vdisplay & 0xff);
	wr(HDMI_FC_INHBLANK1, hblank >> 8);
	wr(HDMI_FC_INHBLANK0, hblank & 0xff);
	wr(HDMI_FC_INVBLANK, vblank);
	wr(HDMI_FC_HSYNCINDELAY1, hdelay >> 8);
	wr(HDMI_FC_HSYNCINDELAY0, hdelay & 0xff);
	wr(HDMI_FC_VSYNCINDELAY, vdelay);
	wr(HDMI_FC_HSYNCINWIDTH1, hwidth >> 8);
	wr(HDMI_FC_HSYNCINWIDTH0, hwidth & 0xff);
	wr(HDMI_FC_VSYNCINWIDTH, vwidth);

	/* The video path: control periods, preambles, then the clocks. */
	wr(HDMI_FC_CTRLDUR, 12);
	wr(HDMI_FC_EXCTRLDUR, 32);
	wr(HDMI_FC_EXCTRLSPAC, 1);
	wr(HDMI_FC_CH0PREAM, 0x0b);
	wr(HDMI_FC_CH1PREAM, 0x16);
	wr(HDMI_FC_CH2PREAM, 0x21);
	wr(HDMI_FC_PRCONF, 0x10);	/* each pixel once, in and out */

	clkdis = MC_CLKDIS_HDCP | MC_CLKDIS_CEC | MC_CLKDIS_CSC |
	    MC_CLKDIS_AUD | MC_CLKDIS_PREP | MC_CLKDIS_TMDS;
	wr(HDMI_MC_CLKDIS, clkdis);		/* pixel clock on */
	clkdis &= ~MC_CLKDIS_TMDS;
	wr(HDMI_MC_CLKDIS, clkdis);		/* and TMDS */
	wr(HDMI_MC_FLOWCTRL, MC_FLOWCTRL_CSC_BYPASS);
	wr(HDMI_CSC_CFG, 0);

	/*
	 * Packetizer: 24 bits per pixel, no repetition, bypassed - the
	 * board's 0x00, 0x07, 0x00, 0x47 - and the general control packet
	 * left out, which a sink without deep colour must not be sent.
	 */
	wr(HDMI_VP_PR_CD, 0);
	modb(HDMI_FC_DATAUTO3, FC_DATAUTO3_GCP_AUTO, 0);
	wr(HDMI_VP_STUFF, 0x07);
	wr(HDMI_VP_REMAP, 0);
	wr(HDMI_VP_CONF, VP_CONF_BYPASS_EN | VP_CONF_BYPASS_SELECT_VP |
	    VP_CONF_OUTPUT_BYPASS);

	/* Sampler: RGB 8:8:8, and what to stuff the blanking with. */
	wr(HDMI_TX_INVID0, TX_INVID0_RGB888);
	wr(HDMI_TX_INSTUFFING, TX_INSTUFFING_ALL);
	for (r = 0; r < 6; r++)
		wr(HDMI_TX_GYDATA0 + r, 0);

	/* The HDCP block's two plain-video bits: polarities, HDMI or DVI. */
	pol = A_VIDPOLCFG_DE_HIGH;
	if (m->flags & VID_PVSYNC)
		pol |= A_VIDPOLCFG_VSYNC_HIGH;
	if (m->flags & VID_PHSYNC)
		pol |= A_VIDPOLCFG_HSYNC_HIGH;
	modb(HDMI_A_VIDPOLCFG, A_VIDPOLCFG_MASK, pol);
	modb(HDMI_A_HDCPCFG0, A_HDCPCFG0_HDMI, hdmi_mode ? A_HDCPCFG0_HDMI : 0);

	if ((r = phy_configure(m->dot_clock)) != OK)
		return r;

	/*
	 * The frame composer can miss a register write while it is busy with
	 * the one before; a TMDS reset and one more write of a composer
	 * register is the documented cure (one write for this revision).
	 */
	wr(HDMI_MC_SWRSTZ, (uint8_t)~MC_SWRSTZ_TMDS);
	wr(HDMI_FC_INVIDCONF, rd(HDMI_FC_INVIDCONF));

	log_info(&fb_log, "hdmi: %dx%d at %d kHz, %s, PHY locked\n",
	    m->hdisplay, m->vdisplay, m->dot_clock, hdmi_mode ? "HDMI" : "DVI");
	return OK;
}

void
dwhdmi_force_color(int on, uint8_t red, uint8_t green, uint8_t blue)
{
	wr(HDMI_FC_DBGTMDS2, red);
	wr(HDMI_FC_DBGTMDS1, green);
	wr(HDMI_FC_DBGTMDS0, blue);
	wr(HDMI_FC_DBGFORCE, on ? FC_DBGFORCE_VIDEO : 0);
}

/*
 * What the board's Linux had in the registers this file writes, at
 * 1280x720.  Two lines differ by design and are marked so: Linux sends HDMI
 * with limited-range RGB, so its CSC clock is on and its data path goes
 * through the converter.
 */
static const struct {
	const char *name;
	uint16_t reg;
	uint8_t val;
	const char *note;
} reference[] = {
	{ "FC_INVIDCONF",  HDMI_FC_INVIDCONF,	0xf8, ": HDMI there, DVI here" },
	{ "FC_INHACTV0",   HDMI_FC_INHACTV0,	0x00, NULL },
	{ "FC_INHACTV1",   HDMI_FC_INHACTV1,	0x05, NULL },
	{ "FC_INHBLANK0",  HDMI_FC_INHBLANK0,	0x72, NULL },
	{ "FC_INHBLANK1",  HDMI_FC_INHBLANK1,	0x01, NULL },
	{ "FC_INVACTV0",   HDMI_FC_INVACTV0,	0xd0, NULL },
	{ "FC_INVACTV1",   HDMI_FC_INVACTV1,	0x02, NULL },
	{ "FC_INVBLANK",   HDMI_FC_INVBLANK,	0x1e, NULL },
	{ "FC_HSYNCINDELAY0", HDMI_FC_HSYNCINDELAY0, 0x6e, NULL },
	{ "FC_HSYNCINWIDTH0", HDMI_FC_HSYNCINWIDTH0, 0x28, NULL },
	{ "FC_VSYNCINDELAY", HDMI_FC_VSYNCINDELAY, 0x05, NULL },
	{ "FC_VSYNCINWIDTH", HDMI_FC_VSYNCINWIDTH, 0x05, NULL },
	{ "FC_CTRLDUR",	   HDMI_FC_CTRLDUR,	0x0c, NULL },
	{ "FC_EXCTRLDUR",  HDMI_FC_EXCTRLDUR,	0x20, NULL },
	{ "FC_CH0PREAM",   HDMI_FC_CH0PREAM,	0x0b, NULL },
	{ "FC_PRCONF",	   HDMI_FC_PRCONF,	0x10, NULL },
	{ "TX_INVID0",	   HDMI_TX_INVID0,	0x01, NULL },
	{ "TX_INSTUFFING", HDMI_TX_INSTUFFING,	0x07, NULL },
	{ "VP_STUFF",	   HDMI_VP_STUFF,	0x07, NULL },
	{ "VP_CONF",	   HDMI_VP_CONF,	0x47, NULL },
	{ "MC_CLKDIS",	   HDMI_MC_CLKDIS,	0x6c, ": CSC clock on there" },
	{ "MC_FLOWCTRL",   HDMI_MC_FLOWCTRL,	0x01, ": CSC in path there" },
	{ "A_VIDPOLCFG",   HDMI_A_VIDPOLCFG,	0x1a, NULL },
	{ "A_HDCPCFG0",	   HDMI_A_HDCPCFG0,	0x01, ": HDMI there, DVI here" },
	{ "PHY_CONF0",	   HDMI_PHY_CONF0,	0x2e, NULL },
	{ "PHY_STAT0",	   HDMI_PHY_STAT0,	0x01, ": bit 1 is the monitor" },
};

static const struct {
	uint8_t reg;
	uint16_t val;
} phy_reference[] = {
	{ PHY_CPCE_CTRL, 0x0072 }, { PHY_CURRCTRL, 0x0000 },
	{ PHY_GMPCTRL, 0x0001 }, { PHY_MSM_CTRL, 0x0006 },
	{ PHY_TXTERM, 0x0004 }, { PHY_CKSYMTXCTRL, 0x8009 },
	{ PHY_VLEVCTRL, 0x0272 },
};

void
dwhdmi_report(void)
{
	unsigned i;
	uint16_t v;
	uint8_t b;

	for (i = 0; i < NELEM(reference); i++) {
		b = rd(reference[i].reg);
		log_info(&fb_log, "hdmi %-17s 0x%04x = 0x%02x (reference "
		    "0x%02x)%s%s\n", reference[i].name, reference[i].reg, b,
		    reference[i].val,
		    b == reference[i].val ? "" : " DIFFERS",
		    b != reference[i].val && reference[i].note != NULL ?
		    reference[i].note : "");
	}
	for (i = 0; i < NELEM(phy_reference); i++) {
		if (phy_read(phy_reference[i].reg, &v) != OK) {
			log_info(&fb_log, "hdmi phy 0x%02x: no answer\n",
			    phy_reference[i].reg);
			continue;
		}
		log_info(&fb_log, "hdmi phy 0x%02x = 0x%04x (reference "
		    "0x%04x)%s\n", phy_reference[i].reg, v,
		    phy_reference[i].val,
		    v == phy_reference[i].val ? "" : " DIFFERS");
	}
}
