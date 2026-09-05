/*
 * Stage 3 measurements: what does a message of a given size cost?
 *
 * The question the benchmark exists to answer is not "how fast is a copy"
 * but "how much of an IPC is the copy". Growing struct message is only
 * expensive if the copy is a large part of the total, and the total is
 * dominated by the trap: entry from EL0 saves thirty-four registers, 272
 * bytes, which is already more than four times the current message. That
 * ratio is the number the decision turns on, and it is the one thing here
 * that QEMU can be trusted to get roughly right, because it is a ratio of
 * instruction counts rather than an absolute time.
 *
 * What QEMU can and cannot say is written out at the top of bench.c.
 */

#ifndef _AARCH64_BENCH_H_
#define _AARCH64_BENCH_H_

#include <stdint.h>

/*
 * The kernel-side half: copy shapes and sizes, measured with interrupts
 * masked. Called once from kernel_main(), before the first process starts.
 */
void bench_run(void);

/*
 * The EL0 half, driven by the program in user.S through SYS_BENCH. The
 * process brackets a loop of system calls with these two, and the kernel
 * reads the counter on either side; the loop itself runs at EL0 with
 * interrupts enabled, because that is how a real IPC runs.
 */
void bench_start(void);
void bench_stop(uint64_t id, uint64_t iters);

/*
 * The tick count, so that bench_stop() can report how many timer interrupts
 * landed inside a measured window instead of leaving the reader to wonder.
 * It lives in start.c, next to the tick handler; when there is a generic
 * kernel this is its clock.c and the accessor goes away.
 */
uint64_t clock_ticks(void);

#endif /* _AARCH64_BENCH_H_ */
