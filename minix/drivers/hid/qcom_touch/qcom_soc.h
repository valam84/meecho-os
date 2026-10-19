/*
 * The parts of a Qualcomm SoC a peripheral driver has to set up for itself
 * when nothing like Linux's clock and pin frameworks runs: the TLMM (pins)
 * and the GCC (branch clocks).  Only the SDM660's tables are known
 * (sdm660_tlmm.h, sdm660_gcc.h, generated from the Linux sources by
 * port/clover/gen-sdm660-tables.py); a tree whose TLMM or GCC is another
 * SoC's is refused by qsoc_init().  Registers: port/clover/hw-touch.md.
 */
#ifndef _QCOM_SOC_H
#define _QCOM_SOC_H

#include <stdint.h>
#include <minix/fdt.h>

/* Pull codes of the TLMM's CFG register. */
#define QSOC_PULL_NONE		0
#define QSOC_PULL_DOWN		1
#define QSOC_PULL_KEEPER	2
#define QSOC_PULL_UP		3

/*
 * Find and map the TLMM and the GCC of the tree.  dtb is the caller's
 * copy, used during the call only.
 */
int qsoc_init(const void *dtb);

/* The GCC alone, for a driver that sets no pins (dwc3_ecm). */
int qsoc_init_gcc(const void *dtb);

/* The GCC node's phandle: what a "clocks" or "resets" entry points at. */
uint32_t qsoc_gcc_phandle(void);

/*
 * Looks that write nothing, for a driver that must see a block powered
 * and clocked before it so much as reads it - on this SoC a read of an
 * unclocked block can stall the bus.  Each takes a device tree number,
 * returns 1 for on (asserted, for a reset), 0 for off, ENOENT for a number
 * the table does not have; *reg gets the register read, *name the SoC's
 * name for it (either may be NULL).
 */
int qsoc_clk_on(unsigned id, uint32_t *reg, const char **name);
int qsoc_gdsc_on(unsigned id, uint32_t *reg, const char **name);
int qsoc_bcr_asserted(unsigned id, uint32_t *reg, const char **name);

/* Hold a block reset (assert) or let it go; ENOENT: not in the table. */
int qsoc_bcr_set(unsigned id, int assert);

/*
 * The root (RCG) a branch clock hangs off: CMD_RCGR and CFG_RCGR, OK, or
 * ENOENT when it has none of its own.  CFG_RCGR's source is bits 10:8 and
 * its half-divider bits 4:0 (divide by (n + 1) / 2).
 */
int qsoc_clk_root(unsigned id, uint32_t *cmd, uint32_t *cfg);

/*
 * Where a branch's enable bit is: the register (CBCR or vote) and the bit,
 * and whether its status may not be checked (*nocheck).  For a driver that
 * sets and clears the bit itself, through its own mapping - the Adreno
 * driver, whose host model must see every write it makes.
 */
int qsoc_clk_gate(unsigned id, uint32_t *en, unsigned *bit, int *nocheck);

/* The mux value of function "name" on GPIO pin (0 is "gpio"), or -1. */
int qsoc_func(unsigned pin, const char *name);

/* Pin set up: mux, drive in mA (2..16), pull, output enable. */
int qsoc_pin(unsigned pin, int func, unsigned drive_ma, unsigned pull,
	int output);
void qsoc_pin_set(unsigned pin, int high);
int qsoc_pin_get(unsigned pin);

/*
 * Apply a pin configuration node (a "pinctrl-0" target): its "pins",
 * "function", "drive-strength" and bias-* properties, child nodes too.
 */
int qsoc_pinctrl(const void *dtb, uint32_t phandle);

/* The GPIO numbers such a node names, into pins[max]; nothing is set. */
unsigned qsoc_pinctrl_pins(const void *dtb, uint32_t phandle, unsigned *pins,
	unsigned max);

/*
 * Turn on the GCC branch clock with device tree number id; if it hangs off
 * a root of its own and hz is 19200000, that root is set to the crystal
 * first (the only rate this code knows to make).
 */
int qsoc_clk_enable(unsigned id, uint32_t hz);

/* The phandle a node of the walk carries, 0 if none. */
uint32_t qsoc_phandle(const struct fdt_node *node);

#endif /* _QCOM_SOC_H */
