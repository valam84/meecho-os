#ifndef __CLOCK_AARCH64_H__
#define __CLOCK_AARCH64_H__

/*
 * The one thing the generic clock code calls into the architecture for: the
 * timer interrupt has arrived, do whatever this timer needs to be told and
 * then account for the tick. On AArch64 the timer is the architected generic
 * timer, reached through system registers rather than a device, so there is
 * no controller to find and no mapping to make.
 */
void arch_timer_int_handler(void);

#endif /* __CLOCK_AARCH64_H__ */
