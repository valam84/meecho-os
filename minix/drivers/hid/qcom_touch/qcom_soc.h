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

/*
 * Turn on the GCC branch clock with device tree number id; if it hangs off
 * a root of its own and hz is 19200000, that root is set to the crystal
 * first (the only rate this code knows to make).
 */
int qsoc_clk_enable(unsigned id, uint32_t hz);

/* The phandle a node of the walk carries, 0 if none. */
uint32_t qsoc_phandle(const struct fdt_node *node);

#endif /* _QCOM_SOC_H */
