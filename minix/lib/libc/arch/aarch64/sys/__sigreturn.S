/* This routine is the low-level code for returning from signals.   */
/* It calls _sigreturn, which is the normal "system call" routine. */
/* Both __sigreturn and _sigreturn are needed. */
#include <machine/asm.h>

/*
 * The kernel points the handler's return address here with the stack
 * pointer at the sigframe it built, whose first word is sf_scp
 * (see struct sigframe_sigcontext in <machine/frame.h>). sigreturn() never
 * returns, so the frame is left in place rather than popped.
 */
IMPORT(sigreturn)
ENTRY(__sigreturn)
	ldr	x0, [sp]		/* sigframe.sf_scp -> first argument */
	b	_C_LABEL(sigreturn)	/* sigreturn(struct sigcontext *sf_scp) */
END(__sigreturn)
