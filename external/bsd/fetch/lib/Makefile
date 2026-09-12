# $NetBSD: Makefile,v 1.8 2011/08/17 09:20:14 christos Exp $

LIB=		fetch
SRCS=		fetch.c common.c ftp.c http.c file.c
DPSRCS= 	ftperr.h httperr.h
INCS=		fetch.h
INCSDIR=	/usr/include
MAN=		fetch.3
CLEANFILES=	ftperr.h httperr.h

.include <bsd.init.mk>

CPPFLAGS+=	-I.
CPPFLAGS+=	-DNETBSD 
CPPFLAGS+=	-DFTP_COMBINE_CWDS
CPPFLAGS+=	-DINET6

.if (${MKCRYPTO} != "no")
CPPFLAGS+=	-DWITH_SSL

LDADD=		-lssl -lcrypto

LIBDPLIBS+=	ssl	${NETBSDSRCDIR}/crypto/external/bsd/openssl/lib/libssl \
		crypto	${NETBSDSRCDIR}/crypto/external/bsd/openssl/lib/libcrypto
.endif

CLEANFILES+=	ftperr.h httperr.h

.PATH: ${LIBFETCHDIR}

ftperr.h: ${LIBFETCHDIR}/ftp.errors ${LIBFETCHDIR}/errlist.sh
	${HOST_SH} ${LIBFETCHDIR}/errlist.sh ftp_errlist FTP \
	    ${LIBFETCHDIR}/ftp.errors > ${.TARGET}

httperr.h: ${LIBFETCHDIR}/http.errors ${LIBFETCHDIR}/errlist.sh
	${HOST_SH} ${LIBFETCHDIR}/errlist.sh http_errlist HTTP \
	    ${LIBFETCHDIR}/http.errors > ${.TARGET}

# GCC 8+: strncpy(locale, setlocale(...), sizeof(locale)) with no room
# kept for the NUL.  A locale name is a few bytes, the buffer is 64; the
# code is imported and stays as it is.  One diagnostic, one file.
COPTS.http.c+=	${CC_WNO_STRINGOP_TRUNCATION}

.include <bsd.lib.mk>
