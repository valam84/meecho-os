#ifndef _BSP_TIMER_H_
#define _BSP_TIMER_H_

#ifndef __ASSEMBLER__

#include <stdint.h>

#include "intr.h"

/* Start a periodic tick at freq hertz. */
void bsp_timer_init(unsigned freq);

/* Stop it. */
void bsp_timer_stop(void);

/*
 * Attach the tick handler and let the interrupt through. Separate from
 * bsp_timer_init() because the earm contract has it that way: the hardware is
 * programmed first and the line is unmasked only once someone is listening.
 */
int bsp_register_timer_handler(const irq_handler_t handler);

/*
 * Acknowledge one tick at the hardware and set up the next. The tick handler
 * calls this; on earm it is what arch_clock.c's timer_int_handler() does
 * before touching anything of its own.
 */
void bsp_timer_int_handler(void);

/*
 * The free-running counter, and how fast it runs.
 *
 * On earm this is a memory-mapped counter whose page also gets mapped into
 * user space, published through arm_frclock so that libc can read the time
 * without a system call. AArch64 does not need any of that: the counter is
 * CNTPCT_EL0, which EL0 can read directly once CNTKCTL_EL1 allows it. The
 * mapping machinery has no counterpart here and should not grow one.
 */
uint64_t bsp_timer_counter(void);
unsigned bsp_timer_counter_hz(void);

#endif /* __ASSEMBLER__ */

#endif /* _BSP_TIMER_H_ */
