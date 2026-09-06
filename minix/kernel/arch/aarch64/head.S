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
 *
 * The kernel is linked at its upper-half virtual address but entered at its
 * physical one, so this code runs KERNEL_VA_OFFSET below where the linker
 * thinks it is. Everything here is addressed PC-relatively - adr, adrp/add -
 * which yields the physical address of a symbol while the MMU is off and the
 * virtual address of the same symbol afterwards. An absolute reference would
 * carry the upper-half value and fault.
 *
 * This file includes none of the kernel's headers, and that is deliberate:
 * it uses no constant that anything else defines, so there is nothing to
 * keep in step. The one number it shares with the link script,
 * KERNEL_VA_OFFSET, is handed to both by Makefile.inc.
 *
 * Where control goes: pre_init() sets up the translation tables, turns the
 * MMU on and leaves for the upper half, where it calls kmain(). It does not
 * come back - the return address here is a physical one, and the identity
 * map that would make it work is taken away as part of the move. The two
 * 32-bit ports return from pre_init into head.S and switch stacks there;
 * they can, because their kernel and user halves live in one page table.
 *
 * This is the tree build's copy of arch/aarch64/bringup/head.S, which is
 * where this code was written and debugged during stage 2. The two stay side
 * by side until the whole architecture layer has moved over and the bring-up
 * kernel can be retired.
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

/*
 * ICC_SRE_EL2: SRE selects the GICv3 system register interface for EL2,
 * Enable lets EL1 use its own without trapping to EL2.
 */
#define ICC_SRE_EL2_SRE		(1 << 0)
#define ICC_SRE_EL2_ENABLE	(1 << 3)

	.section .text.boot, "ax"
	.globl	_start
_start:
	/*
	 * The arm64 Linux image header.
	 *
	 * This is what makes the image something a loader recognises as a
	 * kernel, and that recognition is what gets the device tree handed
	 * over: QEMU passes the blob in x0 only to an image it is willing to
	 * treat as one, and U-Boot's "booti" wants the same header. An ELF
	 * given to -kernel is started with x0 = 0 instead, which is a kernel
	 * that cannot find out what machine it is on.
	 *
	 * The header is 64 bytes and starts with two instruction slots, since
	 * control arrives at offset 0. The first branches over the rest of it.
	 *
	 * text_offset is zero on purpose. The loader places the image at the
	 * 2 MiB-aligned base of RAM plus this offset, so zero puts it exactly
	 * at _kern_phys_base, where the link script already expects it. The
	 * usual Linux value of 0x80000 would land the image somewhere the
	 * linker did not put it, which is the mismatch that kept the flat
	 * image unusable.
	 */
	b	_start_boot		/* code0: branch over the header */
	.long	0			/* code1 */
	/*
	 * text_offset: where in RAM the loader should put the image, counted
	 * from the 2 MiB-aligned base. It has to agree with _kern_phys_base
	 * in kernel.lds, and 2 MiB is what QEMU chooses when this is zero -
	 * so saying it here rather than leaving it zero makes the agreement
	 * explicit instead of accidental.
	 */
	.quad	0x200000		/* text_offset */
	.quad	__image_size		/* image_size, computed in kernel.lds */
	.quad	0			/* flags: 4 KiB pages, LE, any base */
	.quad	0			/* res2 */
	.quad	0			/* res3 */
	.quad	0			/* res4 */
	.ascii	"ARM\x64"		/* magic */
	.long	0			/* res5: no PE header */

_start_boot:
	/*
	 * The loader leaves the physical address of the device tree blob in
	 * x0. It is the only copy we are given, so keep it in a register the
	 * C prologue will preserve until it can be handed to pre_init.
	 */
	mov	x21, x0

	/*
	 * Only the boot CPU goes on. The kernel is uniprocessor for now and
	 * there is exactly one boot stack; a secondary core running this
	 * code would quietly corrupt it. Park the others rather than leaving
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

	/*
	 * Let EL1 reach a GICv3 CPU interface.
	 *
	 * On GICv3 the CPU interface is not memory but the ICC_* system
	 * registers, and EL1 may only touch them once EL2 has said so:
	 * ICC_SRE_EL2.SRE selects the system register interface for EL2
	 * itself, and ICC_SRE_EL2.Enable is what keeps EL1 accesses from
	 * trapping. Without this the driver's very first mrs takes an
	 * undefined instruction trap - and only on this entry path, so a
	 * kernel that boots perfectly under plain -M virt would die under
	 * -M virt,virtualization=on, which is the branch a real board takes.
	 *
	 * Guarded on ID_AA64PFR0_EL1.GIC, bits [27:24]: zero means this CPU
	 * has no system register interface at all and ICC_SRE_EL2 does not
	 * exist to be written. ICH_HCR_EL2 is cleared for the same reason
	 * Linux clears it - the virtual interface is nothing this kernel
	 * uses, and firmware need not have left it off.
	 */
	mrs	x0, id_aa64pfr0_el1
	ubfx	x0, x0, #24, #4
	cbz	x0, no_gicv3
	mrs	x0, icc_sre_el2
	orr	x0, x0, #ICC_SRE_EL2_SRE
	orr	x0, x0, #ICC_SRE_EL2_ENABLE
	msr	icc_sre_el2, x0
	isb
	msr	ich_hcr_el2, xzr
no_gicv3:

	/* Fake an exception return into EL1, landing at at_el1. */
	mov	x0, #SPSR_EL1H
	msr	spsr_el2, x0
	adr	x0, at_el1
	msr	elr_el2, x0
	eret

at_el1:
	/*
	 * From here on the CPU is at EL1 with the MMU off.
	 *
	 * Put SCTLR_EL1 into a known state on both entry paths, not just the
	 * one that came through EL2. Firmware that hands over at EL1 leaves
	 * its own value behind - QEMU's differs from the architectural reset
	 * value in half a dozen bits - and the MMU is later turned on by
	 * setting three bits in whatever happens to be here. Two entry paths
	 * that enable translation from different SCTLR values are two
	 * different kernels, and only one of them gets tested.
	 *
	 * Built with mov/movk rather than "ldr =": a literal pool load is a
	 * data access, and the bits that govern data accesses are exactly the
	 * ones not yet known to be sane.
	 */
	mov	x0, #(SCTLR_EL1_RES1 & 0xffff)
	movk	x0, #(SCTLR_EL1_RES1 >> 16), lsl #16
	msr	sctlr_el1, x0
	isb

	/*
	 * Let EL1 use FP and SIMD, and keep trapping EL0.
	 *
	 * The kernel's own C is built with -mgeneral-regs-only, so this looks
	 * unnecessary until it runs: it links against libminc, and the AArch64
	 * procedure call standard makes every variadic function save q0..q7 on
	 * entry. The first printf() of early boot therefore executes "ldr q31"
	 * and traps - before fpu_init() exists to have set this, and before
	 * there are vectors to report it, which is a machine that stops
	 * without a word.
	 *
	 * CPACR_EL1.FPEN = 0b01 is "trap EL0, allow EL1". EL0 stays trapped
	 * because nothing saves those registers across an exception yet; see
	 * fpu.c.
	 */
	mov	x0, #(1 << 20)
	msr	cpacr_el1, x0
	isb

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
	bl	pre_init

	/*
	 * Not reached: pre_init() leaves for the upper half and calls
	 * kmain() there. If it ever does return, stopping is more useful
	 * than running into whatever follows.
	 */
park_secondary:
	wfe
	b	park_secondary

	.size	_start, . - _start

/*
 * Boot stack. 16 KiB is generous for early boot and costs nothing; the
 * per-CPU kernel stacks come later, from the architecture layer's process
 * code.
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
