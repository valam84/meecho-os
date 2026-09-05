#ifndef _BSP_TIMER_H_
#define _BSP_TIMER_H_

#include "kernel/kernel.h"
#include "kernel/type.h"	/* irq_handler_t */

/*
 * The tick source, as the board support package provides it. The same four
 * calls ARM has.
 *
 * On this architecture the timer is the ARM generic timer, which is part of
 * the architecture rather than of the board: a counter and a comparator
 * reached through system registers, at a frequency the firmware reports in
 * CNTFRQ_EL0. So the driver carries over from one board to the next with
 * nothing changed - not even the interrupt, which comes out of the device
 * tree.
 *
 * That is the reason to prefer it over the BCM2711's System Timer as the
 * tick: the System Timer is a memory-mapped peripheral with board-specific
 * compare channels, two of which belong to the VideoCore firmware.
 */
void bsp_timer_init(unsigned freq);
void bsp_timer_stop(void);
void bsp_timer_int_handler(void);
int bsp_register_timer_handler(irq_handler_t handler);

#endif /* _BSP_TIMER_H_ */
