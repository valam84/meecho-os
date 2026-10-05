/*
 * ctxfp: does swapcontext() keep d8..d15?
 *
 * AAPCS64 makes the low halves of v8..v15 callee-saved, like x19..x28, and
 * GCC keeps values in them across calls. A context switch is a call, so
 * getcontext()/setcontext() have to carry them - also with _UC_IGNFPU, which
 * libmthread sets and which only says "the FP state as a whole may be
 * skipped". VFS lost a lock pair kept in d15 across a call that switched
 * worker threads (port/PORTING-LOG.md, 2026-10-05).
 *
 * The other context overwrites d8..d15 and switches back; the first then
 * checks them. Prints "ctxfp: ok" or the first register that changed.
 */
#include <stdio.h>
#include <stdint.h>
#include <ucontext.h>

static ucontext_t main_ctx, other_ctx;
static char other_stack[16384] __attribute__((aligned(16)));

#define SET(n, v) __asm__ volatile("fmov d" #n ", %0" :: "r"((uint64_t)(v)))
#define GET(n, v) __asm__ volatile("fmov %0, d" #n : "=r"(v))

static void
other(void)
{
	SET(8, 0xdead0008); SET(9, 0xdead0009); SET(10, 0xdead000a);
	SET(11, 0xdead000b); SET(12, 0xdead000c); SET(13, 0xdead000d);
	SET(14, 0xdead000e); SET(15, 0xdead000f);
	swapcontext(&other_ctx, &main_ctx);
}

int
main(void)
{
	uint64_t v[8];
	int i, bad = 0;

	getcontext(&other_ctx);
	other_ctx.uc_stack.ss_sp = other_stack;
	other_ctx.uc_stack.ss_size = sizeof(other_stack);
	other_ctx.uc_link = &main_ctx;
	other_ctx.uc_flags |= _UC_IGNSIGM | _UC_IGNFPU;
	makecontext(&other_ctx, other, 0);
	main_ctx.uc_flags |= _UC_IGNSIGM | _UC_IGNFPU;

	SET(8, 0x1008); SET(9, 0x1009); SET(10, 0x100a); SET(11, 0x100b);
	SET(12, 0x100c); SET(13, 0x100d); SET(14, 0x100e); SET(15, 0x100f);
	swapcontext(&main_ctx, &other_ctx);
	GET(8, v[0]); GET(9, v[1]); GET(10, v[2]); GET(11, v[3]);
	GET(12, v[4]); GET(13, v[5]); GET(14, v[6]); GET(15, v[7]);

	for (i = 0; i < 8; i++) {
		if (v[i] != (uint64_t)(0x1008 + i)) {
			printf("ctxfp: d%d is %#llx after swapcontext, "
			    "expected %#x\n", i + 8, (unsigned long long)v[i],
			    0x1008 + i);
			bad = 1;
		}
	}
	if (!bad)
		printf("ctxfp: ok\n");
	return bad;
}
