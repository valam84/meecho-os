/*
 * The shapes a message copy can have.
 *
 * Stage 3 asks what a message costs, and the answer depends as much on how
 * the copy is written as on how long the message is. A byte loop pays four
 * instructions per byte; a pair loop pays four per sixteen. Comparing message
 * sizes measured with one shape against sizes measured with another says
 * nothing, so all three are here, written out, and measured the same way.
 *
 * In assembly rather than C because the point is to measure a known sequence.
 * At -O2 the compiler is free to turn any of these into any of the others, or
 * into a call to memcpy that does not exist here, and then the benchmark
 * would be measuring the compiler's opinion rather than the instructions.
 *
 * These copy kernel memory to kernel memory and cannot fault. The fault-safe
 * versions, which is what an IPC path actually uses, are in usercopy.S.
 */

	.text

/*
 * bench_copy_null(dst, src, len)
 *
 * The call and the return, and nothing in between. Whatever the measured
 * loop costs, it also costs this; subtracting it is what separates the copy
 * from the harness around it.
 */
	.globl	bench_copy_null
bench_copy_null:
	ret

	.size	bench_copy_null, . - bench_copy_null

/*
 * bench_copy_bytes(dst, src, len) - one byte per iteration.
 *
 * This is what usercopy.S does today, and what a copy has to fall back to
 * when neither end is aligned and the length is arbitrary.
 */
	.globl	bench_copy_bytes
bench_copy_bytes:
	cbz	x2, 2f
1:
	ldrb	w3, [x1], #1
	strb	w3, [x0], #1
	subs	x2, x2, #1
	b.ne	1b
2:
	ret

	.size	bench_copy_bytes, . - bench_copy_bytes

/*
 * bench_copy_words(dst, src, len) - eight bytes per iteration.
 *
 * Length must be a multiple of 8. A message is a struct of aligned fields at
 * an aligned address, so this is the weakest assumption a message copy needs.
 */
	.globl	bench_copy_words
bench_copy_words:
	cbz	x2, 2f
1:
	ldr	x3, [x1], #8
	str	x3, [x0], #8
	subs	x2, x2, #8
	b.ne	1b
2:
	ret

	.size	bench_copy_words, . - bench_copy_words

/*
 * bench_copy_pairs(dst, src, len) - sixteen bytes per iteration.
 *
 * Length must be a multiple of 16. That is not a free assumption: it is why
 * the candidate payload sizes in bench.c are 56, 72, 88 and 120 rather than
 * the round-looking 64, because message is eight bytes of header plus the
 * payload and it is those totals that have to be a multiple of 16.
 */
	.globl	bench_copy_pairs
bench_copy_pairs:
	cbz	x2, 2f
1:
	ldp	x3, x4, [x1], #16
	stp	x3, x4, [x0], #16
	subs	x2, x2, #16
	b.ne	1b
2:
	ret

	.size	bench_copy_pairs, . - bench_copy_pairs
