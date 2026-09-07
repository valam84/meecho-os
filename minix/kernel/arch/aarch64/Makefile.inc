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

.PATH:	${HERE}

# The GIC sits here and not in a board support package on purpose: every
# AArch64 machine has one, and which version it is and where it lives are
# read from the device tree, exactly as psci.c reads its conduit. QEMU's virt
# machine on its own presents either version depending on gic-version=, so a
# build-time choice would be wrong for a single board, never mind two.
#
# The console followed it there, and for a stronger reason: the two machines
# do not even have the same part. QEMU's virt machine has a PL011, the RK3566
# of the CB2 has a DesignWare 8250, and one kernel has to print on both -
# development happens in QEMU and the target is the board. serial.c reads
# /chosen/stdout-path and hands the two calls of <bsp_serial.h> to pl011.c or
# dw8250.c.
SRCS+=	head.S arch_system.c memory.c cache.c direct_tty_utils.c \
	pg_utils.c pre_init.c protect.c arch_do_vmctl.c \
	arch_reset.c arch_clock.c psci.c fpu.c \
	gic.c gicv2.c gicv3.c \
	serial.c pl011.c dw8250.c \
	usercopy.S exception.c vectors.S trap.c hw_intr.c fpu_asm.S

# CONFIG_SMP and CONFIG_MAX_CPUS are set for the whole tree in
# share/mk/bsd.own.mk, because the scheduler needs them as much as the kernel
# does. All that is left here is what an SMP kernel is built from.
.ifdef CONFIG_SMP
SRCS+=	arch_smp.c spinlock.S
.endif

.include "bsp/qemu-virt/Makefile.inc"

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
