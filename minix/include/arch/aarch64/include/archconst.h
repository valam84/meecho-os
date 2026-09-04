#ifndef _AARCH64_CONST_H
#define _AARCH64_CONST_H

/*
 * Ticks per second of the system clock. earm runs at 1000 and i386 at 60;
 * the AArch64 bring-up kernel was written against 100, which is what its
 * generic timer driver programs. This is a tunable, not a property of the
 * machine, and the number to revisit once the scheduler is measured on real
 * hardware rather than under QEMU.
 */
#define DEFAULT_HZ        100

#endif /* #ifndef _AARCH64_CONST_H */
