/*
 * The qcom_cpufreq stand: the service's register accesses go to the model.
 *
 * run.sh puts this file in front of every file of the service (-include).
 * It includes the real qcom_cpufreq.h, where cf_rd()/cf_wr() are one LDR or
 * STR each, and then renames their calls to the model's functions.  The
 * service is not changed by a line.  The model sees every access at the
 * moment the service makes it, and judges it by this SoC's rules: no L or
 * MODE for the PLL the cores run from, the mux onto a locked PLL only, no
 * frequency the rail's set point does not hold, no write to anything but a
 * cluster's mux and PLL.
 */
#ifndef _SHIM_REGS_HOOK_H
#define _SHIM_REGS_HOOK_H
#include "qcom_cpufreq.h"
uint32_t model_rd(const struct cf_region *r, uint32_t off);
void model_wr(const struct cf_region *r, uint32_t off, uint32_t v);
#define cf_rd(r, off)		model_rd((r), (off))
#define cf_wr(r, off, v)	model_wr((r), (off), (v))
#endif
