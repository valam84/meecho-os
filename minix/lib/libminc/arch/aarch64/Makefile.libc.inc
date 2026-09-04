########################################################################
#
# Shared libc with userspace (/common/lib/libc)
#
# What NetBSD wrote in assembly for aarch64 is taken from there; the rest
# comes from the portable C versions. i386 and earm carry a larger
# assembly set, and earm adds EABI and soft-float helpers that AArch64,
# with hardware division and floating point, does not need.
#
.for f in \
	memchr.o memmove.o \
	strchr.o strcmp.o strcpy.o strlcpy.o strncmp.o strncpy.o strrchr.o
${f} ${f:C/\.o/.bc/}:	${LIBCOMMONCDIR}/string/${f:C/\.o/.c/}
${f} ${f:C/\.o/.go/}:	${LIBCOMMONCDIR}/string/${f:C/\.o/.c/}
OBJS+=	${f}
CLEANFILES+= ${f}

.if ${USE_BITCODE:Uno} == "yes"
OBJS+=	${f:C/\.o/.bc/}
CLEANFILES+= ${f:C/\.o/.bc/}
.endif # ${USE_BITCODE:Uno} == "yes"
.endfor

CPPFLAGS.strlcpy.c+=		-D_LIBC

.for f in \
	byte_swap_2.o byte_swap_4.o byte_swap_8.o
${f} ${f:C/\.o/.bc/}:	${LIBCOMMONCARCHDIR}/gen/${f:C/\.o/.S/}
${f} ${f:C/\.o/.go/}:	${LIBCOMMONCARCHDIR}/gen/${f:C/\.o/.S/}
OBJS+=	${f}
CLEANFILES+= ${f}

.if ${USE_BITCODE:Uno} == "yes"
OBJS+=	${f:C/\.o/.bc/}
CLEANFILES+= ${f:C/\.o/.bc/}
.endif # ${USE_BITCODE:Uno} == "yes"
.endfor

.for f in \
	memcmp.o memcpy.o memset.o \
	strcat.o strlen.o
${f} ${f:C/\.o/.bc/}:	${LIBCOMMONCARCHDIR}/string/${f:C/\.o/.S/}
${f} ${f:C/\.o/.go/}:	${LIBCOMMONCARCHDIR}/string/${f:C/\.o/.S/}
OBJS+=	${f}
CLEANFILES+= ${f}

.if ${USE_BITCODE:Uno} == "yes"
OBJS+=	${f:C/\.o/.bc/}
CLEANFILES+= ${f:C/\.o/.bc/}
.endif # ${USE_BITCODE:Uno} == "yes"
.endfor
