#ifndef __AARCH64_WATCHDOG_H__
#define __AARCH64_WATCHDOG_H__

#include "kernel/kernel.h"

/*
 * Nothing of its own, as on ARM: the watchdog of kernel/watchdog.h is the
 * x86 NMI performance-counter watchdog, and its architecture hook is the
 * struct in that header. AArch64 could carry the same thing on the PMU
 * overflow interrupt one day; until it does, arch_watchdog_init() is what
 * the architecture layer has to answer, and it answers that there is none.
 */

#endif /* __AARCH64_WATCHDOG_H__ */
