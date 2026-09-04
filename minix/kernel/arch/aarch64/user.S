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
