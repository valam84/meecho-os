# $NetBSD: Makefile.inc,v 1.6 2026/04/30 13:31:47 simonb Exp $

.include <bsd.own.mk>

.include "${.PARSEDIR}/../Makefile.inc"

LIBINSTALL != cd ${.PARSEDIR}/../lib && ${PRINTOBJDIR}

BINDIR?=	/usr/sbin
CPPFLAGS+=	-DBINDIR='"${BINDIR}"'

DPADD+=	${LIBINSTALL}/libinstall.a
LDADD+=	-L${LIBINSTALL} -linstall -ltermcap

DPADD+=		${LIBNETPGPVERIFY} ${LIBFETCH} ${LIBSSL} ${LIBCRYPTO}
LDADD+=		-lnetpgpverify -lfetch -lssl -lcrypto

DPADD+=		${LIBARCHIVE}
LDADD+=		-larchive
.if !defined(__MINIX)
# MEECHO: no zstd in this tree and libarchive here predates it.
.include "${NETBSDSRCDIR}/external/bsd/zstd/Makefile.zstd"
PROGDPLIBS+=	${ZSTDDPLIBS}	# XXX needed by libarchive
.endif

DPADD+=		${LIBZ} ${LIBBZ2}
LDADD+=		-lz -lbz2

DPADD+=		${LIBLZMA}
LDADD+=		-llzma
.if !defined(__MINIX)
# MEECHO: no libpthread; nothing linked here asks for it.
DPADD+=		${LIBPTHREAD}
LDADD+=		-lpthread
.endif
