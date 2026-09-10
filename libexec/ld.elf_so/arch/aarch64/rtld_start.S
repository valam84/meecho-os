/* $NetBSD: rtld_start.S,v 1.1 2014/08/10 05:47:37 matt Exp $ */

/*-
 * Copyright (c) 2014 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Matt Thomas of 3am Software Foundry.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <machine/asm.h>

RCSID("$NetBSD: rtld_start.S,v 1.1 2014/08/10 05:47:37 matt Exp $")

/*
 * void _rtld_start(void (*cleanup)(void), const Obj_Entry *obj,
 *    struct ps_strings *ps_strings);
 *
 * X0		= NULL
 * X1		= NULL
 * X2		= ps_strings
 * X30 (LR)	= 0
 * X29 (FP)	= 0
 */

ENTRY_NP(_rtld_start)
	mov	x24, x2			/* save ps_strings */

	adrp	x1, :got:_DYNAMIC	/* load _DYNAMIC offset from GOT */
	ldr	x1, [x1, #:got_lo12:_DYNAMIC]

	adrp	x0, _DYNAMIC		/* get &_DYNAMIC */
	add	x0, x0, #:lo12:_DYNAMIC
	sub	x25, x0, x1		/* relocbase = &_DYNAMIC - GOT:_DYNAMIC */
	mov	x1, x25			/* pass as 2nd argument */
	bl	_rtld_relocate_nonplt_self

	sub	sp, sp, #16		/* reserve space for returns */
	mov	x0, sp			/* pointer to reserved space */
	mov	x1, x25			/* pass relocbase */
	bl	_rtld
	mov	x17, x0			/* save entry point */

	ldp	x0, x1, [sp], #16	/* pop cleanup & obj_main */
	mov	x2, x24			/* restore ps_strings */

	br	x17			/* call saved entry point */
END(_rtld_start)

/*
 * Upon entry from the PLT0 entry that ld(1) generates:
 *
 *	X16		= &PLTGOT[2]
 *	X17		= &_rtld_bind_start - PLT0 loaded it from PLTGOT[2]
 *	[SP, #0]	= &PLTGOT[n], the slot to bind: PLT0 pushed the X16
 *			  that the PLTn entry had set up
 *	[SP, #8]	= the caller's X30, also pushed by PLT0
 *	X30		= the caller's return address, still live
 *
 * The version this file was imported with expected &PLTGOT[n] in X17 and
 * left PLT0's sixteen bytes on the stack.  Neither holds for the PLT that
 * ld(1) emits on AArch64: X17 is the resolver's own address, so the
 * relocation index came out as the difference of two unrelated addresses,
 * and the bound function was entered with a stack two words short.
 *
 * The floating point argument registers are saved as well.  A lazily bound
 * call is meant to be invisible to the caller, the callee may take its
 * arguments in V0..V7, and the resolver is ordinary C that may use them.
 */
ENTRY_NP(_rtld_bind_start)
	sub	sp, sp, #224		/* reserve stack space */
	stp	x29, x30, [sp, #80]	/* save FP & LR */
	add	x29, sp, #80		/* get new FP */
	str	x24, [sp, #64]		/* save callee-saved register */
	stp	x6, x7, [sp, #48]	/* save arguments */
	stp	x4, x5, [sp, #32]	/* save arguments */
	stp	x2, x3, [sp, #16]	/* save arguments */
	stp	x0, x1, [sp, #0]	/* save arguments */
	stp	q0, q1, [sp, #96]	/* save FP arguments */
	stp	q2, q3, [sp, #128]
	stp	q4, q5, [sp, #160]
	stp	q6, q7, [sp, #192]

	ldr	x17, [sp, #224]		/* &PLTGOT[n], as PLT0 pushed it */
	mov	x24, x17		/* preserve across _rtld_bind */
	sub	x1, x17, x16		/* x1 = &PLTGOT[n] - &PLTGOT[2] */
	lsr	x1, x1, #3		/* x1 = n - 2 */
	sub	x1, x1, #1		/* .rela.plt index: slots start at 3 */
	ldur	x0, [x16, #-8]		/* get obj ptr from &PLTGOT[1] */
	bl	_rtld_bind
	str	x0, [x24]		/* save address in PLTGOT[n] */
	mov	x17, x0			/* save address */

	ldp	x0, x1, [sp, #0]	/* restore arguments */
	ldp	x2, x3, [sp, #16]	/* restore arguments */
	ldp	x4, x5, [sp, #32]	/* restore arguments */
	ldp	x6, x7, [sp, #48]	/* restore arguments */
	ldr	x24, [sp, #64]		/* restore callee-saved register */
	ldp	x29, x30, [sp, #80]	/* restore FP & LR */
	ldp	q0, q1, [sp, #96]	/* restore FP arguments */
	ldp	q2, q3, [sp, #128]
	ldp	q4, q5, [sp, #160]
	ldp	q6, q7, [sp, #192]
	add	sp, sp, #240		/* reclaim our frame and PLT0's */
	br	x17			/* call bound function */
END(_rtld_bind_start)
