/*
 * AArch64 kernel entry point.
 *
 * Control arrives here from the boot loader with the MMU off, caches in an
 * unknown state, and the CPU at either EL2 or EL1 depending on what the
 * firmware did. The job of this file is to reach a state where C code can
 * run at EL1: one CPU, a stack, and a zeroed BSS.
 *
 * Nothing here may touch memory outside the kernel image. There is no MMU
 * yet, so every address is physical, and the only thing known to exist is
 * what the loader placed in RAM.
 */

/*
 * Bits that read as one in SCTLR_EL1 on ARMv8.0. Writing zero to the whole
 * register would technically be writing zero to reserved-one bits, which is
 * unpredictable. QEMU tolerates it; real hardware need not.
 */
#define SCTLR_EL1_RES1	((1 << 11) | (1 << 20) | (1 << 22) | \
			 (1 << 23) | (1 << 28) | (1 << 29))

/* HCR_EL2.RW: EL1 executes in AArch64 rather than AArch32. */
#define HCR_EL2_RW	(1 << 31)

/*
 * SPSR value used to fake an exception return into EL1. EL1h means EL1 using
 * its own stack pointer (SP_EL1) rather than sharing SP_EL0. DAIF is set so
 * that interrupts stay masked until there is something to handle them.
 */
#define SPSR_EL1H	0x3c5

	.section .text.boot, "ax"
	.globl	_start
_start:
	/*
	 * The loader leaves the physical address of the device tree blob in
	 * x0. Nothing consumes it yet, but this is the only copy we are
	 * given, so keep it in a register the C prologue will preserve.
	 */
	mov	x21, x0

	/*
	 * Only the boot CPU goes on. The kernel is uniprocessor for now and
	 * there is exactly one stack; a secondary core running this code
	 * would quietly corrupt it. Park the others rather than leaving
	 * them loose.
	 *
	 * Aff0 is enough to identify the boot CPU on the boards we care
	 * about. This will need revisiting for SMP, which is the point of
	 * the whole exercise, so it is deliberately conservative: anything
	 * that is not CPU 0 stops here.
	 */
	mrs	x1, mpidr_el1
	and	x1, x1, #0xff
	cbnz	x1, park_secondary

	/* Normalise the exception level: enter C at EL1 either way. */
	mrs	x0, CurrentEL
	lsr	x0, x0, #2
	cmp	x0, #2
	b.ne	at_el1

	/*
	 * Entered at EL2. Configure EL1 and drop into it.
	 */

	/* EL1 is AArch64, not AArch32. */
	mov	x0, #HCR_EL2_RW
	msr	hcr_el2, x0

	/*
	 * Let EL1 read the physical counter and timers. Without this the
	 * first thing the timer code does at EL1 traps, which is a confusing
	 * way to discover the problem.
	 */
	mrs	x0, cnthctl_el2
	orr	x0, x0, #3		/* EL1PCTEN | EL1PCEN */
	msr	cnthctl_el2, x0
	msr	cntvoff_el2, xzr

	/* Give EL1 a defined starting state: MMU and caches off. */
	ldr	x0, =SCTLR_EL1_RES1
	msr	sctlr_el1, x0

	/* Fake an exception return into EL1, landing at at_el1. */
	mov	x0, #SPSR_EL1H
	msr	spsr_el2, x0
	adr	x0, at_el1
	msr	elr_el2, x0
	eret

at_el1:
	/*
	 * From here on the CPU is at EL1 with the MMU off.
	 */

	/* Mask everything until the exception vectors exist. */
	msr	daifset, #0xf

	/* Stack grows down from the top of the boot stack. */
	adrp	x0, boot_stack_top
	add	x0, x0, :lo12:boot_stack_top
	mov	sp, x0

	/*
	 * Zero the BSS. C code assumes it, and the loader guarantees
	 * nothing about the contents of memory beyond the image.
	 */
	adrp	x0, __bss_start
	add	x0, x0, :lo12:__bss_start
	adrp	x1, __bss_end
	add	x1, x1, :lo12:__bss_end
1:
	cmp	x0, x1
	b.hs	2f
	str	xzr, [x0], #8
	b	1b
2:

	/* Hand the device tree pointer to C as the first argument. */
	mov	x0, x21
	bl	kernel_early_main

	/*
	 * kernel_early_main() is not expected to return. If it does,
	 * stopping is more useful than running into whatever follows.
	 */
park_secondary:
	wfe
	b	park_secondary

	.size	_start, . - _start

/*
 * Boot stack. 16 KiB is generous for early boot and costs nothing; the
 * real stacks come from the scheduler once there is one.
 *
 * AArch64 requires SP to be 16-byte aligned whenever it is used.
 */
	.section .bss.boot, "aw", @nobits
	.align	12
	.globl	boot_stack
boot_stack:
	.space	16384
	.globl	boot_stack_top
boot_stack_top:
