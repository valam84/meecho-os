/*
 * Stage 3: measuring what a message size costs.
 *
 * What this can and cannot tell you
 * ---------------------------------
 * The clock is CNTPCT_EL0, and under QEMU that counter is derived from the
 * emulator's virtual clock, not from a Cortex-A72. QEMU's TCG translates each
 * guest instruction into host instructions and executes them; there is no
 * pipeline, no cache, no store buffer and no prefetcher. So:
 *
 *   - it CAN compare sequences that differ in how many instructions and how
 *     many memory operations they issue. Sixteen bytes per iteration against
 *     one byte per iteration is such a difference, and so is 64 bytes against
 *     128.
 *   - it CAN give the ratio between a trap and a copy, because both are
 *     mostly instruction counts.
 *   - it CANNOT say anything about cache lines. Whether a 64-byte message
 *     straddles two lines, whether the destination is already in L1, whether
 *     the copy evicts something - none of that is modelled. The alignment row
 *     below is measured for exactly this reason: it comes out flat, and that
 *     flatness is a fact about QEMU rather than about the hardware.
 *   - it CANNOT be read as nanoseconds on real silicon. Only the ratios
 *     between the rows mean anything.
 *
 * Running under -icount makes the numbers deterministic: virtual time then
 * advances with the instruction count instead of with the host's load, so
 * CNTPCT stops measuring the machine this is running on and starts measuring
 * the guest. Makefile.bringup's bench target does that; see the comment
 * there. Both ways are worth running, and they should agree on the ratios.
 *
 * One more thing the numbers below do not include: these copies run out of
 * the same two buffers every iteration, so on real hardware they would always
 * hit in L1. A real IPC copies into a different process's message slot each
 * time. That is a cost of the message size that nothing here can measure, and
 * it argues in the same direction as everything else - it grows with the
 * message.
 */

#include <stdint.h>

#include "bench.h"
#include "bsp_timer.h"
#include "kprint.h"

/*
 * Candidate sizes.
 *
 * These are whole messages, not payloads: struct message is eight bytes of
 * header - endpoint_t m_source and int m_type, both still four bytes on
 * LP64 - followed by the payload, and the whole thing is what gets copied.
 *
 * The ladder is set by __ALIGNED(16) on message. A payload of 64 would make
 * the message 72 bytes and sizeof would round it to 80, wasting the eight
 * bytes it just bought; the payload sizes that give a whole message with no
 * waste are 56, 72, 88 and 120. Those are the four here. 256 is not a
 * candidate, it is there to show the shape of the curve past the candidates.
 */
static const uint64_t sizes[] = { 64, 80, 96, 128, 256 };
#define NSIZES		(sizeof(sizes) / sizeof(sizes[0]))

/*
 * Iterations. Enough that the loop dominates the two counter reads around it
 * and that the result is many counter ticks rather than a few - CNTPCT runs
 * at 62.5 MHz under QEMU, so one tick is 16 ns and a single copy is well
 * under that.
 */
#define ITERS		20000

/* bench_copy.S */
typedef void (*copy_fn)(void *dst, const void *src, uint64_t len);
void bench_copy_null(void *dst, const void *src, uint64_t len);
void bench_copy_bytes(void *dst, const void *src, uint64_t len);
void bench_copy_words(void *dst, const void *src, uint64_t len);
void bench_copy_pairs(void *dst, const void *src, uint64_t len);

/*
 * Aligned to a cache line so that the offset row below is the only thing that
 * moves the buffers off one. The Cortex-A72 line is 64 bytes.
 */
static uint8_t bench_src[512] __attribute__((aligned(64)));
static uint8_t bench_dst[512] __attribute__((aligned(64)));

static uint64_t bench_t0;
static uint64_t bench_ticks0;

/*
 * What an empty call through the same function pointer costs. Every row is
 * measured with the same harness, so subtracting this leaves the copy.
 */
static uint64_t bench_baseline;

static inline uint64_t
irq_mask_save(void)
{
	uint64_t daif;

	__asm__ volatile("mrs %0, daif" : "=r"(daif));
	__asm__ volatile("msr daifset, #2" ::: "memory");
	return daif;
}

static inline void
irq_restore(uint64_t daif)
{
	__asm__ volatile("msr daif, %0" :: "r"(daif) : "memory");
}

/*
 * Nanoseconds per iteration, to two decimals, right-aligned in a column.
 *
 * Two decimals because the interesting differences are small: a 64-byte pair
 * copy is four iterations of four instructions, and under -icount that is
 * tens of nanoseconds of virtual time. There is no printf here and this is
 * not the place to write one.
 */
static void
put_rate(uint64_t ticks, uint64_t iters)
{
	uint64_t hz = bsp_timer_counter_hz();
	uint64_t ns, hundredths, whole, frac, digits, pad;

	if (hz == 0 || iters == 0) {
		kputs("       n/a");
		return;
	}

	ns = ticks * 1000000000ULL / hz;
	hundredths = ns * 100ULL / iters;
	whole = hundredths / 100;
	frac = hundredths % 100;

	for (digits = 1, pad = whole; pad >= 10; pad /= 10)
		digits++;
	for (pad = digits + 3; pad < 10; pad++)
		kputs(" ");

	kput_dec(whole);
	kputs(".");
	if (frac < 10)
		kputs("0");
	kput_dec(frac);
}

/*
 * One measured loop.
 *
 * The call goes through a function pointer so that every shape - including
 * the empty one - pays the same call overhead, which then cancels when the
 * baseline is subtracted. Interrupts are masked: a tick landing inside the
 * window would be charged to the copy, and at this granularity one tick is
 * worth thousands of copies.
 */
static uint64_t
time_copies(copy_fn fn, uint64_t len, uint64_t off)
{
	uint64_t daif, start, end, i;

	daif = irq_mask_save();
	start = bsp_timer_counter();
	for (i = 0; i < ITERS; i++)
		fn(bench_dst + off, bench_src + off, len);
	end = bsp_timer_counter();
	irq_restore(daif);

	return end - start;
}

static void
bench_copy_table(void)
{
	uint64_t baseline = bench_baseline;
	unsigned i;

	kputs("\nkernel-to-kernel copy, ns per copy, call overhead removed\n");
	kputs("  bytes    1B/iter   8B/iter  16B/iter\n");

	for (i = 0; i < NSIZES; i++) {
		uint64_t len = sizes[i];
		uint64_t b = time_copies(bench_copy_bytes, len, 0);
		uint64_t w = time_copies(bench_copy_words, len, 0);
		uint64_t p = time_copies(bench_copy_pairs, len, 0);

		kputs("   ");
		if (len < 100)
			kputs(" ");
		kput_dec(len);
		put_rate(b - baseline, ITERS);
		put_rate(w - baseline, ITERS);
		put_rate(p - baseline, ITERS);
		kputs("\n");
	}

	kputs("  call     ");
	put_rate(baseline, ITERS);
	kputs("  (measured separately and subtracted above)\n");
}

/*
 * The same 64-byte copy, moved off the cache line.
 *
 * struct message is __ALIGNED(16), so a 64-byte message is guaranteed to sit
 * on a 16-byte boundary and nothing more; three quarters of the time it
 * straddles two 64-byte lines. On a Cortex-A72 that is two line fills instead
 * of one. Here it will come out flat, because QEMU has no lines to straddle -
 * which is the point of printing it: the row is evidence about the emulator,
 * and it says this particular question has to be settled by reasoning.
 */
static void
bench_alignment(void)
{
	uint64_t on, off;

	on = time_copies(bench_copy_pairs, 64, 0) - bench_baseline;
	off = time_copies(bench_copy_pairs, 64, 16) - bench_baseline;

	kputs("\n64-byte pair copy against cache-line position\n");
	kputs("  on line  ");
	put_rate(on, ITERS);
	kputs("\n  straddling");
	put_rate(off, ITERS);
	kputs("\n  QEMU models no caches, so a difference here would be a\n");
	kputs("  surprise - and the absence of one says nothing about an A72.\n");
}

void
bench_run(void)
{
	unsigned i;

	for (i = 0; i < sizeof(bench_src); i++)
		bench_src[i] = (uint8_t)i;

	kputs("\nmessage size benchmark\n");
	kputs("----------------------\n");
	kputs("counter     : ");
	kput_dec(bsp_timer_counter_hz());
	kputs(" Hz\n");
	kputs("iterations  : ");
	kput_dec(ITERS);
	kputs("\n");

	bench_baseline = time_copies(bench_copy_null, 0, 0);

	bench_copy_table();
	bench_alignment();
}

/*
 * The EL0 half. The process calls SYS_BENCH around a loop of system calls;
 * these two ends read the counter. Interrupts stay enabled inside the window
 * because a real IPC is preemptible, so the tick count is reported alongside
 * the result rather than eliminated.
 */
void
bench_start(void)
{
	static int header;

	if (!header) {
		header = 1;
		kputs("\n  what one crossing into the kernel costs\n");
	}

	bench_ticks0 = clock_ticks();
	bench_t0 = bsp_timer_counter();
}

void
bench_stop(uint64_t id, uint64_t iters)
{
	static const char *const names[] = {
		"null system call             ",
		"one way,  64-byte message    ",
		"one way,  80-byte message    ",
		"one way,  96-byte message    ",
		"one way, 128-byte message    ",
		"there and back,  64 bytes    ",
		"there and back,  80 bytes    ",
		"there and back,  96 bytes    ",
		"there and back, 128 bytes    ",
	};
	uint64_t delta = bsp_timer_counter() - bench_t0;
	uint64_t ticks = clock_ticks() - bench_ticks0;

	kputs("  ");
	if (id < sizeof(names) / sizeof(names[0]))
		kputs(names[id]);
	else
		kput_dec(id);

	put_rate(delta, iters);
	kputs(" ns   ticks in window: ");
	kput_dec(ticks);
	kputs("\n");
}
