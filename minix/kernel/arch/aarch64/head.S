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

/* ELF relocation type of every entry in the kernel's .rela.dyn (kernel.lds). */
#define R_AARCH64_RELATIVE	1027

	.section .text.head0, "ax"
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
	 * from a 2 MiB-aligned base. Zero, as Linux has had it since 5.8.
	 *
	 * It used to be 0x200000, to agree with _kern_phys_base explicitly.
	 * The kernel no longer cares where it is put (it moves itself, below),
	 * and zero is the value loaders are known to accept: QEMU still puts
	 * the image at 0x40200000, U-Boot with flags bit 3 leaves it where
	 * it is, and a Qualcomm ABL - the Mi Pad 4's - boots mainline Linux
	 * with exactly this header. Some ABLs compare the field against what
	 * they expect and refuse the image otherwise.
	 */
	.quad	0			/* text_offset */
	/*
	 * image_size is not here: kernel.lds writes it between the two halves
	 * of the header with QUAD(). The image is linked -pie, and under -pie
	 * bfd turns a .quad of any link-script symbol into a dynamic
	 * relocation - a write into .text.boot, which -z text refuses. A value
	 * the link script writes itself is never relocated.
	 */
	.section .text.head1, "ax"
	/*
	 * flags: 4 KiB pages, little-endian, and - bit 3 - the 2 MiB-aligned
	 * base of this image may be anywhere in physical memory.
	 *
	 * Bit 3 is not decoration, and zero here is not "any base": it is the
	 * opposite. U-Boot's booti_setup() reads that bit and, without it,
	 * relocates the image to the base of RAM plus text_offset, because a
	 * Linux kernel cannot address memory below its own load address
	 * through the linear map. On the CB2 that means moving the image to
	 * 0x00400000 - RAM there starts at 0x00200000, the first two megabytes
	 * being TF-A's. The kernel would survive that now (it relocates itself,
	 * below), but there is no reason to pay for a copy, and the bit is
	 * simply true: any base will do.
	 *
	 * With the bit set, U-Boot computes the destination from where the
	 * image already is and leaves it alone. QEMU never consulted the flags
	 * at all - it places a flat image at the base of RAM plus text_offset,
	 * which on virt is exactly 0x40200000, and that is why the mismatch
	 * stayed invisible until a real loader was involved.
	 */
	.quad	(1 << 3)		/* flags */
	.quad	0			/* res2 */
	.quad	0			/* res3 */
	.quad	0			/* res4 */
	.ascii	"ARM\x64"		/* magic */
	.long	0			/* res5: no PE header */

	.section .text.boot, "ax"
_start_boot:
	/*
	 * The loader leaves the physical address of the device tree blob in
	 * x0. It is the only copy we are given, so keep it in a register the
	 * C prologue will preserve until it can be handed to pre_init.
	 */
	mov	x21, x0

	/*
	 * Only the boot CPU goes on here. The others are started later and
	 * deliberately, by smp_init() through PSCI, and they arrive at
	 * _start_ap below with a stack and a number of their own. One that
	 * wandered in here would run the boot core's path on the boot core's
	 * stack.
	 *
	 * Aff0 is enough to tell them apart on the machines this runs on. A
	 * firmware that releases the secondaries at the kernel entry point
	 * rather than holding them - the old spin-table way - would need the
	 * park below; PSCI machines never get here at all.
	 */
	mrs	x1, mpidr_el1
	and	x1, x1, #0xff
	cbnz	x1, park_secondary

	/* Normalise the exception level: enter C at EL1 either way. */
	adr	x9, at_el1
	b	to_el1

/*
 * Return to EL1 at the address in x9, from whichever level this core was
 * started at. Clobbers x9 and x10, and nothing else - _start_boot is holding
 * the device tree pointer in x21 across this.
 *
 * Both entry points come through here. A secondary started by PSCI wakes at
 * the exception level of whoever called CPU_ON, which is EL1 on the machines
 * in front of us; a firmware that hands it EL2 instead is allowed to, and
 * Linux makes the same allowance for the same reason.
 */
to_el1:
	mrs	x10, CurrentEL
	lsr	x10, x10, #2
	cmp	x10, #2
	b.ne	already_el1

	/*
	 * Entered at EL2. Configure EL1 and drop into it.
	 */

	/* EL1 is AArch64, not AArch32. */
	mov	x10, #HCR_EL2_RW
	msr	hcr_el2, x10

	/*
	 * Let EL1 read the physical counter and timers. Without this the
	 * first thing the timer code does at EL1 traps, which is a confusing
	 * way to discover the problem.
	 */
	mrs	x10, cnthctl_el2
	orr	x10, x10, #3		/* EL1PCTEN | EL1PCEN */
	msr	cnthctl_el2, x10
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
	 * These are per-CPU registers, which is why this sits in the shared
	 * path and not in the boot core's: a secondary that came up at EL2
	 * has to do it for itself.
	 *
	 * Guarded on ID_AA64PFR0_EL1.GIC, bits [27:24]: zero means this CPU
	 * has no system register interface at all and ICC_SRE_EL2 does not
	 * exist to be written. ICH_HCR_EL2 is cleared for the same reason
	 * Linux clears it - the virtual interface is nothing this kernel
	 * uses, and firmware need not have left it off.
	 */
	mrs	x10, id_aa64pfr0_el1
	ubfx	x10, x10, #24, #4
	cbz	x10, no_gicv3
	mrs	x10, icc_sre_el2
	orr	x10, x10, #ICC_SRE_EL2_SRE
	orr	x10, x10, #ICC_SRE_EL2_ENABLE
	msr	icc_sre_el2, x10
	isb
	msr	ich_hcr_el2, xzr
no_gicv3:

	/* Fake an exception return into EL1, landing where x9 says. */
	mov	x10, #SPSR_EL1H
	msr	spsr_el2, x10
	msr	elr_el2, x9
	eret

already_el1:
	br	x9

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

	/*
	 * This is CPU 0, and say so before any C runs.
	 *
	 * TPIDR_EL1 is where cpuid reads the core's number from, and on an SMP
	 * build every per-CPU variable in the kernel is indexed by it - from
	 * the first line of kmain(), long before anything has read the device
	 * tree. Its reset value is not architecturally defined, so leaving it
	 * alone would mean indexing those arrays with whatever the firmware
	 * happened to leave. smp_init() renumbers the cores so that this one
	 * really is zero.
	 */
	msr	tpidr_el1, xzr

	/*
	 * Get the image onto a 64 KiB boundary.
	 *
	 * The move below (".rela.dyn") can put the image at any address that
	 * is a multiple of 64 KiB away from where it is linked - that is the
	 * largest alignment in it (k_stacks, the segments). A loader following
	 * the arm64 boot protocol always lands on one. A Qualcomm ABL is not
	 * known to: Linux carried TEXT_OFFSET 0x8000 for years on their
	 * account. So an image found off that grid is first copied up to the
	 * next boundary - never more than 64 KiB, and the header's image_size
	 * claims that much extra (kernel.lds) so the loader leaves room - and
	 * continues from the copy.
	 *
	 * Copied backwards, because the two overlap. The destination must
	 * begin above this loop, or the loop would overwrite itself: the
	 * distance is pushed up by another 64 KiB if it is closer than that.
	 * Only the file's contents are copied - the BSS is cleared below, at
	 * the new address. The MMU is off, so the stores reach memory, and
	 * the instruction cache is invalidated before the jump.
	 */
	adr	x0, _start
	ands	x1, x0, #0xffff
	b.eq	7f
	mov	x2, #0x10000
	sub	x1, x2, x1		/* x1: distance up to the next boundary */
	adr	x7, 7f
	sub	x7, x7, x0		/* how far into the image the loop ends */
	cmp	x1, x7
	b.hs	6f
	add	x1, x1, x2
6:
	adrp	x3, _edata
	add	x3, x3, :lo12:_edata	/* end of what the file holds */
	add	x4, x3, x1
8:
	ldp	x5, x6, [x3, #-16]!
	stp	x5, x6, [x4, #-16]!
	cmp	x3, x0
	b.hi	8b
	dsb	sy
	ic	iallu
	dsb	sy
	isb
	adr	x5, 7f
	add	x5, x5, x1
	br	x5			/* the same place, in the copy */
7:

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

	/*
	 * Move the image to where it was loaded.
	 *
	 * The kernel is linked for _kern_phys_base and loaded wherever the
	 * loader chose - the same place on QEMU and the CB2, somewhere else
	 * entirely on a Qualcomm ABL. Code does not care: it is PC-relative.
	 * Data does, in exactly the slots .rela.dyn lists, each holding an
	 * upper-half address computed for the link base. Each gets the
	 * distance added (kernel.lds; Makefile.inc checks at build time that
	 * the table holds nothing but R_AARCH64_RELATIVE).
	 *
	 * r_offset is the slot's link-time virtual address; with the MMU off
	 * it is reached at its physical one, which is the low 48 bits plus the
	 * distance. The value written is virtual, for the upper half.
	 *
	 * The MMU is off, so the store goes straight to memory. A line the
	 * loader left in a cache for that address would win over it once the
	 * caches come on, so each slot is invalidated after the store - the
	 * same reason ap_boot_params is cleaned to the point of coherency.
	 *
	 * The distance has to keep every alignment the link gave the image:
	 * k_stacks is aligned to twice its size for the guard page
	 * (arch_init() asserts it), the segments to 64 KiB. So it must be a
	 * multiple of 64 KiB, which Makefile.inc checks is the largest
	 * alignment in the image, and which the copy above has made true.
	 * The check stays as the copy's own: if it ever fails there is no
	 * console to say so yet, and running would fail later and further
	 * from the cause.
	 */
	adr	x0, _start
	movz	x1, #(KERNEL_LINK_BASE & 0xffff)
	movk	x1, #((KERNEL_LINK_BASE >> 16) & 0xffff), lsl #16
	sub	x0, x0, x1		/* x0: load address - link address */
	tst	x0, #0xffff
	b.ne	park_secondary
	adrp	x1, __rela_dyn_start
	add	x1, x1, :lo12:__rela_dyn_start
	adrp	x2, __rela_dyn_end
	add	x2, x2, :lo12:__rela_dyn_end
3:
	cmp	x1, x2
	b.hs	5f
	ldp	x3, x4, [x1], #16	/* r_offset, r_info */
	ldr	x5, [x1], #8		/* r_addend */
	cmp	w4, #R_AARCH64_RELATIVE
	b.ne	3b			/* R_AARCH64_NONE: nothing to do */
	and	x3, x3, #0x0000ffffffffffff
	add	x3, x3, x0
	add	x5, x5, x0
	str	x5, [x3]
	dc	ivac, x3
	b	3b
5:
	dsb	sy

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

	.size	_start_boot, . - _start_boot

/*
 * Where a secondary core starts.
 *
 * smp_init() hands this address to PSCI CPU_ON as a physical address, and
 * the logical CPU number as the context PSCI puts in x0. So a core knows
 * which core it is from its first instruction, and needs no table to look
 * itself up in - which matters, because it has no stack yet to look with.
 *
 * The state it arrives in is the same as the boot core's: MMU off, caches
 * unknown, exception level whatever the firmware chose. So it takes the same
 * path through to_el1 and then repeats the same three settings, all of which
 * are per-CPU registers and none of which the boot core could have set on its
 * behalf.
 */
	.globl	_start_ap
	.type	_start_ap, %function
_start_ap:
	mov	x20, x0			/* logical cpu number, from PSCI */

	adr	x9, ap_el1
	b	to_el1

ap_el1:
	/* SCTLR_EL1 to the same known state the boot core starts from. */
	mov	x0, #(SCTLR_EL1_RES1 & 0xffff)
	movk	x0, #(SCTLR_EL1_RES1 >> 16), lsl #16
	msr	sctlr_el1, x0
	isb

	/* FP and SIMD for EL1, trapped for EL0, as on the boot core. */
	mov	x0, #(1 << 20)
	msr	cpacr_el1, x0
	isb

	/* Masked until this core has vectors and a controller of its own. */
	msr	daifset, #0xf

	/*
	 * Translation on, and into the upper half, without a stack.
	 *
	 * Until SCTLR_EL1.M is set this core's data accesses are Device and
	 * go to memory past every cache; from the next instruction they are
	 * cacheable and coherent. Memory written on one side of that switch
	 * and read on the other can come back as what some cache - another
	 * core's - holds instead, so nothing here writes memory at all, and
	 * the one thing it reads is its line of ap_boot_params, which the boot
	 * core cleaned to the point of coherency before CPU_ON. The layout is
	 * struct ap_boot_params in pg_utils.c, checked there at compile time:
	 *   0 MAIR  8 TCR  16 TTBR0  24 TTBR1  32 SCTLR bits  40 SP  48 entry
	 * one 64-byte line per core, indexed by the number PSCI handed over.
	 */
	adrp	x0, ap_boot_params
	add	x0, x0, :lo12:ap_boot_params
	add	x0, x0, x20, lsl #6
	ldp	x1, x2, [x0, #0]
	ldp	x3, x4, [x0, #16]
	ldp	x5, x6, [x0, #32]
	ldr	x7, [x0, #48]

	msr	mair_el1, x1
	msr	tcr_el1, x2
	msr	ttbr0_el1, x3
	msr	ttbr1_el1, x4
	isb

	/* The TLB is not guaranteed to start empty. */
	tlbi	vmalle1
	dsb	nsh
	isb

	mrs	x1, sctlr_el1
	orr	x1, x1, x5
	msr	sctlr_el1, x1
	isb

	/*
	 * Running on the identity map now. The stack and the entry point are
	 * upper-half addresses, and smp_ap_boot() does not come back.
	 */
	mov	sp, x6
	mov	x0, x20
	br	x7

	.size	_start_ap, . - _start_ap

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
