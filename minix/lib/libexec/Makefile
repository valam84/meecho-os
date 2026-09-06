# Makefile for libexec

LIB=	exec
INCS=	libexec.h
SRCS=	exec_elf.c exec_general.c

INCSDIR= /usr/include


# The kernel links this library, and the kernel must not touch an FP or SIMD
# register: nothing saves them on the way into an exception, so a q register
# the kernel used would change the arithmetic of whichever process owns the
# FP state. Its own C is built this way; this says the same about the code it
# borrows. Without it GCC copies and zeroes structures with q29..q31 here and
# the kernel corrupts user state at random.
#
# See minix/kernel/arch/aarch64/fpu.c, which carries the objdump that checks
# it, and port/PORTING-LOG.md, stage 5.3.
.if ${MACHINE_ARCH} == "aarch64"
CFLAGS+=	-mgeneral-regs-only
.endif
.include <bsd.lib.mk>
