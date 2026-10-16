/*
 * dwc3_ecm: a Synopsys DWC3 USB controller in device mode, as a network
 * interface to the host at the other end of the cable (CDC-ECM).  The
 * Mi Pad 4 (SDM660) is the machine it is written for: its one USB-C port,
 * to a Linux hub.  Facts and plan: port/clover/hw-net.md.
 *
 * Layers, each a file: the device tree (dwc3_find.c), the SoC's wrapper
 * around the core (dwc3_qcom.c, for "qcom,dwc3"), the PHY (qusb2.c, for
 * "qcom,sdm660-qusb2-phy"), the driver's life (dwc3_ecm.c).  Another SoC's
 * wrapper or PHY is a file and a line in the tables below them, never a
 * test of which board this is.
 *
 * This milestone (hw-net.md, step 2) only looks: it prints what powers,
 * clocks and resets the blocks have, and reads the blocks themselves only
 * when the GCC shows them powered and clocked.  It writes nothing.
 */
#ifndef _DWC3_ECM_H
#define _DWC3_ECM_H

#include <minix/drivers.h>
#include <minix/fdt.h>
#include <minix/log.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define DWC3_MAXCLK	8
#define DWC3_NAMELEN	24

struct dwc3_glue_ops;
struct dwc3_phy_ops;

struct dwc3_clk {
	uint32_t id;			/* the GCC's number for it */
	char name[DWC3_NAMELEN];	/* by "clock-names" */
};

struct dwc3_dt {
	/*
	 * Every clock, reset and power domain below names one provider - the
	 * SoC's clock controller (GCC) - by this phandle; the tree is refused
	 * if they differ or if its cell counts are not one number each.
	 */
	uint32_t gcc_ref;

	/* The wrapper node, and the controller below it. */
	const struct dwc3_glue_ops *glue;	/* the table entry it matched */
	char glue_node[DWC3_NAMELEN];
	char glue_compat[DWC3_NAMELEN];
	u64_t qscratch_pa, qscratch_sz;	/* the wrapper's own reg */
	struct dwc3_clk clk[DWC3_MAXCLK];
	unsigned nclk;
	int have_pd;
	uint32_t pd;			/* power-domains, the GCC's GDSC */
	uint32_t rst[2];
	unsigned nrst;
	int utmi_as_pipe;		/* qcom,select-utmi-as-pipe-clk */
	u64_t core_pa, core_sz;
	int irq;
	char dr_mode[16];
	char max_speed[16];
	uint32_t phy_phandle;		/* "usb2-phy" */

	/* The PHY. */
	const struct dwc3_phy_ops *phy;
	char phy_compat[40];
	u64_t phy_pa, phy_sz;
	struct dwc3_clk phy_clk[4];
	unsigned nphyclk;
	int have_phy_rst;
	uint32_t phy_rst;
	char supply[3][DWC3_NAMELEN];	/* vdd, vdda-pll, vdda-phy-dpdm */
	uint32_t supply_phandle[3];
	uint32_t trim_phandle;		/* nvmem-cells */

	/* The PHY's tuning fuse: a byte of the QFPROM and its bits. */
	u64_t fuse_base, fuse_size;	/* the QFPROM node's reg */
	u64_t trim_off;			/* the cell's reg, from fuse_base */
	unsigned trim_bit, trim_nbits;
};

extern struct log dwc3_log;

/* dwc3_find.c: the tree, read and checked. OK, ENXIO (no controller). */
int dwc3_find(const void *dtb, struct dwc3_dt *dt);

/* A wrapper (glue) or PHY this driver knows, by compatible string. */
struct dwc3_glue_ops {
	const char *compatible;
	/*
	 * Print what powers, resets and clocks the wrapper and core have.
	 * OK, with the core's registers mapped into *core, when both are
	 * powered, out of reset and clocked - running, as a boot loader that
	 * used them leaves them; EBUSY when not, and then nothing was mapped.
	 */
	int (*report)(const struct dwc3_dt *dt, volatile uint8_t **core);
	/*
	 * Device mode without a VBUS detector of the wrapper's own: tell the
	 * core the session is valid (Linux does this for dr_mode
	 * "peripheral").  Only after report() said OK.
	 */
	void (*peripheral)(void);
};
struct dwc3_phy_ops {
	const char *compatible;
	/* Print the PHY's state; OK when it runs (powered, PLL locked). */
	int (*report)(const struct dwc3_dt *dt);
};
extern const struct dwc3_glue_ops dwc3_qcom_glue;	/* dwc3_qcom.c */
extern const struct dwc3_phy_ops qusb2_phy;		/* qusb2.c */

/*
 * dwc3_dev.c: the core in device mode, taken over as a boot loader left
 * it running: events masked, the core reset, an event buffer of its own,
 * ep0, run.  Then dwc3_dev_poll() on every tick.
 */
int dwc3_dev_start(volatile uint8_t *core);
void dwc3_dev_poll(void);
void dwc3_dev_stop(void);		/* the pull-up off: the host sees it go */

/* The register map of a block, or NULL - mapped once, kept. */
volatile uint8_t *dwc3_map(u64_t pa, u64_t size, const char *what);

/* GCC looks shared by the wrapper and the PHY: 1 on, 0 off, -1 unknown. */
int dwc3_clk_state(const struct dwc3_clk *c, char *buf, size_t size);

#endif /* _DWC3_ECM_H */
