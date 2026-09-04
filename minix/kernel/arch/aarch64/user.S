/*
 * The first user program.
 *
 * Assembled into the kernel's .rodata, which means the kernel maps it
 * read-only and non-executable; proc.c copies it into a page of its own and
 * maps that as user text. It is data to the kernel and code to the process,
 * which is exactly the distinction the two address spaces exist to make.
 *
 * Everything here is PC-relative and free of literal pools, because the code
 * is assembled at one address and executed at another - the kernel's .rodata
 * versus USER_TEXT_VA. Same rule as the boot path, for the same reason, and
 * the whole blob moves as a unit so relative distances survive.
 *
 * The calling convention is x8 for the system call number and x0..x2 for
 * arguments, with the result in x0. See proc.h for why that is a placeholder.
 */

#define SYS_WRITE	0
#define SYS_EXIT	1
#define SYS_GETTICKS	2
#define SYS_NULL	3
#define SYS_MSGCOPY	4
#define SYS_BENCH	5
#define SYS_MSGXCHG	6

/*
 * How many times round each measured loop. Large enough that the two SYS_BENCH
 * calls bracketing it are noise, and that the result is thousands of counter
 * ticks rather than a handful - CNTPCT ticks every 16 ns under QEMU and a
 * system call is faster than that.
 */
#define BENCH_ITERS	10000

/*
 * The clock belongs to the kernel. A process cannot time itself here, and
 * giving it a way to would mean deciding how EL0 reads CNTPCT - a question
 * for a stage that has a userland. So the loop is bracketed by system calls
 * and the kernel reads the counter on either side.
 */
	.macro	BENCH_BEGIN
	mov	x0, #0
	mov	x8, #SYS_BENCH
	svc	#0
	.endm

	.macro	BENCH_END id
	mov	x0, #1
	mov	x1, #\id
	mov	x2, #BENCH_ITERS
	mov	x8, #SYS_BENCH
	svc	#0
	.endm

/*
 * A loop of system calls that each hand the kernel a message of \size bytes,
 * through \call - SYS_MSGCOPY for one crossing, SYS_MSGXCHG for a request and
 * a reply.
 */
	.macro	BENCH_MSG id, size, call
	BENCH_BEGIN
	mov	x10, #BENCH_ITERS
1:
	mov	x0, x11
	mov	x1, #\size
	mov	x8, #\call
	svc	#0
	subs	x10, x10, #1
	b.ne	1b
	BENCH_END \id
	.endm

	.section .rodata, "a"
	.balign	16
	.globl	user_prog_start
user_prog_start:

	/* Say something, through a pointer of our own. */
	adr	x0, user_msg
	mov	x1, #(user_msg_end - user_msg)
	mov	x8, #SYS_WRITE
	svc	#0

	/*
	 * Spin. The point is to still be at EL0 when the timer fires, several
	 * times, so that the kernel can show it saved and restored a process
	 * context rather than merely entered one.
	 *
	 * 0x3000000 is written as a shifted immediate so it assembles to a
	 * single MOVZ instead of a literal load.
	 */
	mov	x9, #0x3000000
1:
	subs	x9, x9, #1
	b.ne	1b

	/*
	 * Now ask the kernel to read from an address that is not ours. The
	 * mapping is not there, so this is a fault the kernel takes on our
	 * behalf, inside copyin - and it has to come back with an error
	 * rather than die.
	 */
	mov	x0, #0x2000000
	mov	x1, #16
	mov	x8, #SYS_WRITE
	svc	#0

	/*
	 * Stage 3: what does one crossing into the kernel cost, and how much
	 * of that is the message?
	 *
	 * The first loop calls a system call that does nothing, so what it
	 * measures is the trap: the vector, thirty-four registers saved and
	 * restored, the dispatch, the return. The others hand the kernel a
	 * message of a candidate size and it copies it in. The difference
	 * between the first row and the rest is the price of the message, and
	 * it is a price paid against the trap, not on its own.
	 *
	 * The sizes are whole messages - eight bytes of header plus payload -
	 * for payloads of 56, 72, 88 and 120. See bench.c for why those.
	 */
	mov	x11, sp
	sub	x11, x11, #256		/* a message of our own to hand over */

	BENCH_BEGIN
	mov	x10, #BENCH_ITERS
1:
	mov	x8, #SYS_NULL
	svc	#0
	subs	x10, x10, #1
	b.ne	1b
	BENCH_END 0

	BENCH_MSG 1, 64,  SYS_MSGCOPY
	BENCH_MSG 2, 80,  SYS_MSGCOPY
	BENCH_MSG 3, 96,  SYS_MSGCOPY
	BENCH_MSG 4, 128, SYS_MSGCOPY

	BENCH_MSG 5, 64,  SYS_MSGXCHG
	BENCH_MSG 6, 80,  SYS_MSGXCHG
	BENCH_MSG 7, 96,  SYS_MSGXCHG
	BENCH_MSG 8, 128, SYS_MSGXCHG

	/* How many ticks did the kernel count while we were running? */
	mov	x8, #SYS_GETTICKS
	svc	#0

	/* Hand that back as the exit status, to prove a value came through. */
	mov	x8, #SYS_EXIT
	svc	#0

	/* SYS_EXIT does not return. If it does, stop rather than run on. */
2:
	b	2b

user_msg:
	.ascii	"hello from EL0\n"
user_msg_end:

	.balign	16
	.globl	user_prog_end
user_prog_end:
