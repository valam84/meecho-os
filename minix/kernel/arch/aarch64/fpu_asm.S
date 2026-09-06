/*
 * Moving the FP/SIMD register file to and from memory.
 *
 * Assembly, and it has to be: every C file in the kernel is built with
 * -mgeneral-regs-only, which is what keeps the compiler from touching these
 * registers anywhere else - so the one place that does touch them cannot be
 * C. Inline assembly would not do either, because that flag makes GCC narrow
 * the .arch it hands the assembler and an "stp q0, q1" would then be
 * rejected in the very file whose job it is.
 *
 * Both routines take a struct fpu_state * in x0. The register file is 512
 * bytes at offset zero, and FPSR and FPCR follow it as two 32-bit words; see
 * <machine/fpu.h>.
 */

/*===========================================================================*
 *				fpu_save_regs				     *
 *===========================================================================*/
/* void fpu_save_regs(struct fpu_state *) */
	.text
	.globl	fpu_save_regs
	.type	fpu_save_regs, %function
fpu_save_regs:
	stp	q0,  q1,  [x0, #0]
	stp	q2,  q3,  [x0, #32]
	stp	q4,  q5,  [x0, #64]
	stp	q6,  q7,  [x0, #96]
	stp	q8,  q9,  [x0, #128]
	stp	q10, q11, [x0, #160]
	stp	q12, q13, [x0, #192]
	stp	q14, q15, [x0, #224]
	stp	q16, q17, [x0, #256]
	stp	q18, q19, [x0, #288]
	stp	q20, q21, [x0, #320]
	stp	q22, q23, [x0, #352]
	stp	q24, q25, [x0, #384]
	stp	q26, q27, [x0, #416]
	stp	q28, q29, [x0, #448]
	stp	q30, q31, [x0, #480]

	mrs	x1, fpsr
	mrs	x2, fpcr
	str	w1, [x0, #512]
	str	w2, [x0, #516]
	ret
	.size	fpu_save_regs, . - fpu_save_regs

/*===========================================================================*
 *			      fpu_restore_regs				     *
 *===========================================================================*/
/* void fpu_restore_regs(const struct fpu_state *) */
	.globl	fpu_restore_regs
	.type	fpu_restore_regs, %function
fpu_restore_regs:
	/*
	 * FPCR first. It selects the rounding mode and which exceptions trap,
	 * and setting it after the registers would mean the loads happened
	 * under whatever the last process left behind. The loads themselves
	 * raise nothing - they are moves - but the order is the one to be in
	 * the habit of.
	 */
	ldr	w1, [x0, #512]
	ldr	w2, [x0, #516]
	msr	fpcr, x2
	msr	fpsr, x1

	ldp	q0,  q1,  [x0, #0]
	ldp	q2,  q3,  [x0, #32]
	ldp	q4,  q5,  [x0, #64]
	ldp	q6,  q7,  [x0, #96]
	ldp	q8,  q9,  [x0, #128]
	ldp	q10, q11, [x0, #160]
	ldp	q12, q13, [x0, #192]
	ldp	q14, q15, [x0, #224]
	ldp	q16, q17, [x0, #256]
	ldp	q18, q19, [x0, #288]
	ldp	q20, q21, [x0, #320]
	ldp	q22, q23, [x0, #352]
	ldp	q24, q25, [x0, #384]
	ldp	q26, q27, [x0, #416]
	ldp	q28, q29, [x0, #448]
	ldp	q30, q31, [x0, #480]
	ret
	.size	fpu_restore_regs, . - fpu_restore_regs
