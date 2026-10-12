#ifndef _AARCH64_HWWD_H_
#define _AARCH64_HWWD_H_

/*
 * The SoC's hardware watchdog: a timer outside the cores that resets the
 * machine unless the kernel restarts it in time. Not to be confused with
 * arch_watchdog.h, which is the x86 NMI watchdog's hook and empty here.
 *
 * hwwd.c finds the part in the device tree and feeds it from the boot core's
 * timer tick; one file per part does the registers. Kept as an int kind
 * rather than a table of function pointers for the reason serial.h gives:
 * the probe runs before the MMU is on.
 */

#define HWWD_NONE	0
#define HWWD_DW		1	/* DesignWare APB watchdog, "snps,dw-wdt" */

void hwwd_probe(phys_bytes dtb);
void hwwd_init(void);
void hwwd_pet(void);

int dw_wdt_start(vir_bytes base, unsigned seconds, u64_t cntfrq,
	u32_t *rate_out, u32_t *ms_out);
void dw_wdt_pet(vir_bytes base);

#endif /* _AARCH64_HWWD_H_ */
