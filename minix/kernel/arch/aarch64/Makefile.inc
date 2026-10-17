# Makefile for arch-dependent kernel code, aarch64.
.include <bsd.own.mk>

HERE=${.CURDIR}/arch/${MACHINE_ARCH}

# Distance between a physical address and the kernel-virtual address of the
# same byte. Stage 2.3 settled the layout: 4 KiB granule, 48-bit addresses,
# physical memory visible in the upper half at a fixed offset.
#
# It is defined here, once, and handed to both the compiler and the linker.
# The C sources need it as a compile-time constant, because early boot works
# out physical addresses with it before anything maps the upper half; the link
# script needs the same number to place the sections. A copy in each would be
# free to drift, and a drift shows up as a machine that dies with the MMU half
# on. See kernel.lds and port/PORTING-LOG.md, stage 2.3.
KERNEL_VA_OFFSET=	0xffff000000000000

CPPFLAGS+=	-DKERNEL_VA_OFFSET=${KERNEL_VA_OFFSET}UL
LDFLAGS+=	-Wl,--defsym=KERNEL_VA_OFFSET=${KERNEL_VA_OFFSET}

# The kernel must not touch FP/SIMD: those registers are not saved on the way
# into an exception, so using them would corrupt user state - and would trap,
# since FP is disabled for EL1 until a process asks for it.
CFLAGS+=	-mgeneral-regs-only

# Reach symbols with PC-relative adrp/add. Early boot depends on it: it is
# what lets the image execute at its physical address before the MMU is on.
CFLAGS+=	-mcmodel=small

# Link position-independent, so that one image runs wherever the loader put
# it. QEMU and U-Boot on the CB2 put it at 0x40200000, where it is linked; a
# Qualcomm ABL (the Mi Pad 4) has no RAM there and chooses for itself. head.S
# applies .rela.dyn before any C runs - see kernel.lds.
#
# The code is not built -fpie: -mcmodel=small is PC-relative already, and the
# whole kernel has single-digit absolute pointers, all in data. "-z text"
# makes that a promise the linker keeps: a relocation in a read-only segment
# fails the link instead of becoming a write to the kernel's own text with
# the MMU off. --no-dynamic-linker drops the .interp a -pie link asks for.
LDFLAGS+=	-Wl,-pie -Wl,--no-dynamic-linker -Wl,-z,text

# The physical address the image is linked for, which head.S subtracts from
# where it actually is. Handed over as a number for the same reason as
# KERNEL_VA_OFFSET, and one more: under -pie bfd turns any reference to a
# link-script symbol into a dynamic relocation, so head.S cannot simply load
# _kern_phys_base - the value would itself need relocating first.
KERNEL_LINK_BASE=	0x40200000
CPPFLAGS+=	-DKERNEL_LINK_BASE=${KERNEL_LINK_BASE}
LDFLAGS+=	-Wl,--defsym=KERNEL_LINK_BASE=${KERNEL_LINK_BASE}

# head.S applies R_AARCH64_RELATIVE and steps over R_AARCH64_NONE, and nothing
# else. Anything else - a symbolic relocation, a GOT slot to be bound - would
# be skipped silently and leave a pointer to where the kernel was linked,
# which on a machine with no RAM there is a fault far from its cause. So the
# check runs after every link and fails the build instead.
#
# The second half checks the other assumption head.S makes: that a load
# address 64 KiB away from the link address keeps every alignment in the
# image. A section asking for more (say a 128 KiB-aligned table) would end
# up misaligned on a machine whose loader picks another base.
realall: kernel.relocs
kernel.relocs: kernel
	${_MKTARGET_CREATE}
	${READELF} -r -W kernel | awk ' \
	    $$3 ~ /^R_AARCH64_/ && $$3 != "R_AARCH64_RELATIVE" && \
	    $$3 != "R_AARCH64_NONE" { bad++; print "kernel: " $$3 " at " $$1 \
	    " - head.S applies only R_AARCH64_RELATIVE" > "/dev/stderr" } \
	    $$3 == "R_AARCH64_RELATIVE" { n++ } \
	    END { if (bad) exit 1; print n + 0 " R_AARCH64_RELATIVE" }' \
	    > ${.TARGET}.tmp
	${READELF} -S -W kernel | awk ' \
	    $$1 ~ /^\[/ && $$NF + 0 > 65536 { bad++; print "kernel: section " \
	    $$2 " wants alignment " $$NF ", head.S keeps only 64 KiB" \
	    > "/dev/stderr" } END { exit bad ? 1 : 0 }'
	mv ${.TARGET}.tmp ${.TARGET}
CLEANFILES+=	kernel.relocs kernel.relocs.tmp

# Frame records, so that a panic can print the kernel's stack: sys.mk builds
# everything with -fomit-frame-pointer, and util_stacktrace() had nothing to
# walk (exception.c). Not in leaf functions: a leaf calls nothing, so it is
# never in the middle of a chain, and a fault in one is reported with pc and
# lr. Added after DBG, so it wins over sys.mk. The price was measured under
# -icount; see port/PORTING-LOG.md, "Стек ядра в панике".
CFLAGS+=	-fno-omit-frame-pointer -momit-leaf-frame-pointer

.PATH:	${HERE}

# The frame buffer console's font, the copy the fb driver uses too. Only the
# font's own directory: the whole of sys/ on the path would put NetBSD's
# kernel headers in front of the ones this kernel is built against.
CPPFLAGS.fbcon.c+=	-I${NETBSDSRCDIR}/sys/dev/wsfont

# There is no board support package on this architecture, and that is a
# result rather than an omission. A BSP is a build-time choice between board
# implementations of one contract; here every question a BSP would answer is
# answered at run time out of the device tree, so one kernel binary runs on
# QEMU's virt machine and on the CB2, and both are exercised by every run.
#
# The GIC went first: every AArch64 machine has one, and which version it is
# and where it lives are read from the tree, exactly as psci.c reads its
# conduit. QEMU's virt machine alone presents either version depending on
# gic-version=, so a build-time choice would be wrong for a single board,
# never mind two.
#
# The console followed, for a stronger reason: the two machines do not even
# have the same part. QEMU's virt machine has a PL011, the RK3566 of the CB2
# a DesignWare 8250, and one kernel has to print on both - development
# happens in QEMU and the target is the board. serial.c reads
# /chosen/stdout-path and hands the two calls of <bsp_serial.h> to pl011.c or
# dw8250.c.
#
# What was left in bsp/qemu-virt was a timer and a reset that carried a
# board's name and no board's code: the ARM generic timer, whose interrupt
# comes out of the tree, and PSCI, whose conduit does. They are
# generic_timer.c and psci_reset.c now. The bsp_*.h headers stayed - they
# are the contract, shared in shape with earm - and moved to include/ with
# the rest of the architecture's headers.
SRCS+=	head.S arch_system.c memory.c cache.c direct_tty_utils.c \
	pg_utils.c pre_init.c protect.c arch_do_vmctl.c \
	arch_reset.c arch_clock.c psci.c qcom_scm.c fpu.c \
	gic.c gicv2.c gicv3.c \
	generic_timer.c psci_reset.c \
	serial.c pl011.c dw8250.c fbcon.c \
	hwwd.c dw_wdt.c \
	usercopy.S pan.c exception.c vectors.S trap.c hw_intr.c fpu_asm.S

# CONFIG_SMP and CONFIG_MAX_CPUS are set for the whole tree in
# share/mk/bsd.own.mk, because the scheduler needs them as much as the kernel
# does. All that is left here is what an SMP kernel is built from.
.ifdef CONFIG_SMP
SRCS+=	arch_smp.c spinlock.S lockwd.c
.endif

# The rest of the architecture layer is written by taking the bring-up kernel
# in arch/aarch64/bringup - mmu.c, trap.c, proc.c, intr.c, usercopy.S
# and the rest, built by its own Makefile.bringup - and fitting it file by
# file to the interfaces in kernel/proto.h. Each file joins SRCS as it lands.
# arch/aarch64 holds no name the generic kernel also uses, so .PATH above is
# unambiguous; the bring-up kernel, which does share names, is a directory
# down and not on it.
#
# The link names what is left: build the kernel and read the undefined
# references. See port/PORTING-LOG.md, stage 4.

# The unpaged object machinery earm and i386 carry is deliberately absent.
# Both start with the MMU off, run pre_init and pg_utils against physical
# addresses, and therefore link a second copy of those objects under a
# __k_unpaged_ symbol prefix. This kernel does not: early code is
# PC-relative, it enables the MMU itself and moves to the upper half in one
# step, so every object exists once. See PORTING-LOG.md, stage 2.3.
