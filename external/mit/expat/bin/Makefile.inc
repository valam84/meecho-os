#	$NetBSD: Makefile.inc,v 1.2 2022/02/23 19:07:35 martin Exp $

BINDIR?=	/usr/bin

CPPFLAGS+=	-I ${EXPATSRCDIR}/lib -I ${EXPATSRCDIR}/../lib/libexpat

.if defined(__MINIX)
# MEECHO: bsd.prog.mk puts -L ${DESTDIR}/usr/lib (MINIX's Gold linker
# workaround) ahead of LDADD, so -lexpat found the libexpat already
# installed - the previous version - and not the one built beside it.
# The archive is named by path instead.
LDADD+=	${EXPATLIB.expat}
.else
LDADD+=	-L${EXPATOBJDIR.expat} -lexpat
.endif
DPADD+=	${EXPATLIB.expat}

.include "../Makefile.inc"
