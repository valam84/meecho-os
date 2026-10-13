# $NetBSD: Makefile.inc,v 1.2 2015/04/15 19:13:46 mrg Exp $

.if !defined(__MINIX)
# MINIX delivers signals through its own frame; see
# minix/lib/libc/arch/aarch64/sys/__sigreturn.S
SRCS+=	__sigaction14_sigtramp.c __sigtramp2.S
.endif
#SRCS+= qdivrem.c

CPPFLAGS += -I.

.if !defined(__MINIX)
# MINIX: not here.  qp.c gives the long double (binary128) helpers GCC
# calls - __addtf3, __divtf3, __trunctfdf2 and the rest - because NetBSD's
# own libgcc is built without them.  The port's compiler is an external GCC
# whose libgcc has them all (soft-fp), and libgcc is on every link line;
# with both, a program that needs one helper from each archive does not
# link: "multiple definition of __floatsitf" (fontconfig, stage 11.4).
.PATH: ${ARCHDIR}/softfloat
SRCS+=	qp.c
CPPFLAGS+=	-DSOFTFLOATAARCH64_FOR_GCC -DEXCEPTIONS_WITH_SOFTFLOAT
CPPFLAGS+=	-DSOFTFLOAT_NEED_FIXUNS
#
# Can't directly use softfloat.c due to .PATH issues with the other source
# in softfloat. Just provide a wrapper which pulls it in via a #include
SRCS.softfloat= softfloat-wrapper.c
SRCS+= ${SRCS.softfloat}
CPPFLAGS+=      -I${ARCHDIR}/softfloat -I${.CURDIR}/softfloat \
		-I${.CURDIR}/softfloat/bits64
.endif
