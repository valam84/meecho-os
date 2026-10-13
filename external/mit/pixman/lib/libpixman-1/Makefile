# NetBSD's reachover (src/external/mit/xorg/lib/pixman, Makefile and
# Makefile.cflags, 2025), cut to this machine: AArch64 with NEON, as NetBSD
# builds it; outside X11, so /usr/include and /usr/lib.  The config header
# is MEECHO's own, beside this file.

NOLINT=	1	# defined

.include <bsd.init.mk>

LIB=		pixman-1

.PATH:	${PIXMANDIST}/pixman

SRCS=					\
	pixman.c			\
	pixman-access.c			\
	pixman-access-accessors.c	\
	pixman-bits-image.c		\
	pixman-combine32.c		\
	pixman-combine-float.c		\
	pixman-conical-gradient.c	\
	pixman-edge.c			\
	pixman-edge-accessors.c		\
	pixman-fast-path.c		\
	pixman-filter.c			\
	pixman-glyph.c			\
	pixman-general.c		\
	pixman-gradient-walker.c	\
	pixman-image.c			\
	pixman-implementation.c		\
	pixman-linear-gradient.c	\
	pixman-matrix.c			\
	pixman-noop.c			\
	pixman-radial-gradient.c	\
	pixman-region16.c		\
	pixman-region32.c		\
	pixman-region64f.c		\
	pixman-solid-fill.c		\
	pixman-timer.c			\
	pixman-trap.c			\
	pixman-utils.c

SRCS+=					\
	pixman-arm.c			\
	pixman-mips.c			\
	pixman-ppc.c			\
	pixman-riscv.c			\
	pixman-x86.c

# ARM64 NEON: part of AArch64, used without asking (pixman-arm.c);
# PIXMAN_DISABLE=arm-neon in the environment turns it off at run time.
SRCS+=		pixman-arm-neon.c			\
		pixman-arma64-neon-asm.S		\
		pixman-arma64-neon-asm-bilinear.S
CPPFLAGS+=	-DUSE_ARM_A64_NEON

CPPFLAGS+=	-DHAVE_CONFIG_H \
		-I${.CURDIR} \
		-I${PIXMANDIST}/pixman

# What pixman's own build asks of the compiler (meson.build): the code
# reads pixels through pointers of other types.
COPTS+=		-fno-strict-aliasing

INCS=		pixman.h pixman-version.h
INCSDIR=	/usr/include/pixman-1

.include <bsd.lib.mk>
