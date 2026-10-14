# Makefile for the framebuffer driver.
PROG=	fb

.include "arch/${MACHINE_ARCH}/Makefile.inc"

SRCS+=	fb_edid.c fb.c fbcon.c

# re-use EDID parsing/validation code from NetBSD.
.PATH: ${NETBSDSRCDIR}/sys/dev/videomode
SRCS+=	edid.c pickmode.c videomode.c vesagtf.c

# Put this dir and the EDID headers (dev/videomode/*.h) in the search path.
CPPFLAGS+= -I${.CURDIR} -I${NETBSDSRCDIR}/sys

# The console's colours are MEECHO's (minix/share/style/palette).
STYLEDIR=	${NETBSDSRCDIR}/minix/share/style
style.h: ${STYLEDIR}/palette ${STYLEDIR}/style.awk
	${_MKTARGET_CREATE}
	${TOOL_AWK} -v out=c -f ${STYLEDIR}/style.awk ${STYLEDIR}/palette \
	    > ${.TARGET}.tmp && mv ${.TARGET}.tmp ${.TARGET}
DPSRCS+=	style.h
CLEANFILES+=	style.h style.h.tmp
CPPFLAGS+=	-I.

DPADD+=	${LIBCHARDRIVER} ${LIBSYS}
LDADD+=	-lchardriver -lsys

.include <minix.service.mk>
