/*
 * Does a machine context carry the FP/SIMD registers?
 *
 * getcontext(3) and setcontext(3) reach the kernel through getmcontext(2)
 * and setmcontext(2), and do_mcontext.c copied the FP save area only under
 * "#if defined(__i386__)".  On aarch64 the process has such an area too, so
 * every context switched to by swapcontext(3) resumed with whatever happened
 * to be in the register file - which is the other context's arithmetic.
 *
 * The probe is one round trip.  main() puts a known pattern in d8..d15 and a
 * known FPCR and FPSR, swapcontext()s into a context that overwrites all
 * three with something else and switches straight back.  What it reads
 * back afterwards is what the kernel put there.
 *
 * Why d8..d15 and not the whole register file: the kernel restores the
 * registers when setmcontext() is called, and library code runs after that
 * and before the resume point - setuctx() returns, setcontext()'s tail runs,
 * getcontext() returns into swapcontext().  That code is entitled to use
 * v0..v7 and v16..v31, and memcpy() in the message path does.  A probe that
 * demanded those back would fail on a correct kernel, and the first version
 * of this one did: v25, v27 and v31 came back holding a memcpy's leftovers
 * while everything else was exactly right.  d8..d15 are the ones the
 * procedure call standard makes callee-saved, so the library preserves
 * whatever it was handed and the value read back is the kernel's.
 *
 * And why the other context switches back with swapcontext() rather than
 * returning through uc_link: because d8..d15 are callee-saved, alt() saves
 * them on entry and puts them back on the way out, so a context that ends by
 * returning hands its caller the register file it was given and the check
 * passes on a kernel that does nothing at all.  That was measured, not
 * feared - with the kernel branch disabled and alt() returning, only FPCR
 * and FPSR reported a loss.  Switching from inside alt() leaves the pattern
 * standing at the moment the context is captured.
 *
 * FPCR is checked as well as the registers, and separately, because the
 * kernel's save area and NetBSD's __fregset_t do not agree on the order of
 * the two control words: FPCR first in the mcontext, FPSR first in struct
 * fpu_state.  A block copy of the whole thing would land the cumulative
 * exception flags in the rounding-mode register and go on computing, so the
 * register file passing is not evidence that the control words did.
 *
 *	aarch64-elf64-minix-gcc -O2 -Wall -o fpctx fpctx.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>

#define ALT_STACK	(64 * 1024)

/* FPCR[23:22] = 0b11: round toward zero.  Nothing else in the word is set,
 * so a value read back that is not this one did not come from us. */
#define FPCR_RZ		0x00c00000UL

/* FPSR[4]: IXC, inexact.  Sticky - set until somebody writes the register -
 * so it survives whatever floating point runs in between. */
#define FPSR_IXC	0x00000010UL

/* What the other context leaves behind, in every register it touches. */
#define ALT_FILL	0xb0

static ucontext_t uc_main, uc_alt;
static unsigned long long out[8];
static unsigned long fpcr_out, fpsr_out;
static volatile int alt_ran;

static void
alt(void)
{
	alt_ran = 1;

	/*
	 * Everything the caller cares about, set to something else: the
	 * registers to a pattern of our own and both control words to zero,
	 * which is round-to-nearest with no exception recorded.  A kernel
	 * that restores nothing leaves all of this in place, and main()
	 * reads it back.
	 */
	__asm__ volatile(
	    "mov	x9, %0\n\t"
	    "fmov	d8, x9\n\t"
	    "fmov	d9, x9\n\t"
	    "fmov	d10, x9\n\t"
	    "fmov	d11, x9\n\t"
	    "fmov	d12, x9\n\t"
	    "fmov	d13, x9\n\t"
	    "fmov	d14, x9\n\t"
	    "fmov	d15, x9\n\t"
	    "msr	fpcr, xzr\n\t"
	    "msr	fpsr, xzr\n\t"
	    :: "i" (ALT_FILL)
	     : "x9", "d8", "d9", "d10", "d11", "d12", "d13", "d14", "d15");

	/*
	 * Back to main() from here rather than by returning: see the note at
	 * the top of this file.  uc_link is set all the same, so that a
	 * failure to switch ends the program rather than running off the end
	 * of a context.
	 */
	if (swapcontext(&uc_alt, &uc_main) != 0) {
		perror("swapcontext (alt)");
		exit(2);
	}
}

int
main(int argc, char **argv)
{
	int i, bad = 0;
	char *stack;

	(void)argc;
	(void)argv;

	if ((stack = malloc(ALT_STACK)) == NULL) {
		perror("malloc");
		return 2;
	}

	memset(&uc_main, 0, sizeof(uc_main));
	memset(&uc_alt, 0, sizeof(uc_alt));

	/*
	 * uc_flags stays zero on purpose.  _UC_IGNFPU would tell getuctx()
	 * and setuctx() to skip the kernel call altogether, and the probe
	 * would then measure nothing at all.
	 */
	if (getcontext(&uc_alt) != 0) {
		perror("getcontext");
		return 2;
	}
	uc_alt.uc_stack.ss_sp = stack;
	uc_alt.uc_stack.ss_size = ALT_STACK;
	uc_alt.uc_link = &uc_main;
	makecontext(&uc_alt, alt, 0);

	/* d8..d15 <- 8 .. 15, and the two control words to known values. */
	__asm__ volatile(
	    "mov	x9, #8\n\t"  "fmov	d8, x9\n\t"
	    "mov	x9, #9\n\t"  "fmov	d9, x9\n\t"
	    "mov	x9, #10\n\t" "fmov	d10, x9\n\t"
	    "mov	x9, #11\n\t" "fmov	d11, x9\n\t"
	    "mov	x9, #12\n\t" "fmov	d12, x9\n\t"
	    "mov	x9, #13\n\t" "fmov	d13, x9\n\t"
	    "mov	x9, #14\n\t" "fmov	d14, x9\n\t"
	    "mov	x9, #15\n\t" "fmov	d15, x9\n\t"
	    "mov	x9, %0\n\t"  "msr	fpcr, x9\n\t"
	    "mov	x9, %1\n\t"  "msr	fpsr, x9\n\t"
	    :: "i" (FPCR_RZ), "i" (FPSR_IXC)
	     : "x9", "d8", "d9", "d10", "d11", "d12", "d13", "d14", "d15");

	if (swapcontext(&uc_main, &uc_alt) != 0) {
		perror("swapcontext");
		return 2;
	}

	/* Read it all back before anything else can touch it. */
	__asm__ volatile(
	    "str	d8, [%2, #0]\n\t"
	    "str	d9, [%2, #8]\n\t"
	    "str	d10, [%2, #16]\n\t"
	    "str	d11, [%2, #24]\n\t"
	    "str	d12, [%2, #32]\n\t"
	    "str	d13, [%2, #40]\n\t"
	    "str	d14, [%2, #48]\n\t"
	    "str	d15, [%2, #56]\n\t"
	    "mrs	%0, fpcr\n\t"
	    "mrs	%1, fpsr\n\t"
	    "msr	fpcr, xzr\n\t"	/* sane rounding again before printf */
	    : "=&r" (fpcr_out), "=&r" (fpsr_out)
	    : "r" (out) : "memory");

	if (!alt_ran) {
		printf("the second context never ran: this run proves "
		    "nothing\n");
		return 2;
	}

	for (i = 0; i < 8; i++)
		if (out[i] != (unsigned long long)(i + 8)) {
			printf("d%d lost: 0x%llx, wanted %d%s\n", i + 8,
			    out[i], i + 8,
			    out[i] == ALT_FILL ? " (the other context's)" : "");
			bad++;
		}

	if (fpcr_out != FPCR_RZ) {
		printf("FPCR lost: 0x%lx, wanted 0x%lx%s\n", fpcr_out,
		    FPCR_RZ,
		    fpcr_out == FPSR_IXC ? " (that is our FPSR: the two "
		    "control words were copied as a block)" : "");
		bad++;
	}
	if (!(fpsr_out & FPSR_IXC)) {
		printf("FPSR lost: 0x%lx, wanted bit 0x%lx set\n", fpsr_out,
		    FPSR_IXC);
		bad++;
	}

	printf(bad ? "FP state does not survive swapcontext\n" :
	    "FP state survives swapcontext\n");
	return bad != 0;
}
