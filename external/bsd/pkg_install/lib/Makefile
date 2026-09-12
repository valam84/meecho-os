# $NetBSD: Makefile,v 1.9 2010/04/23 20:56:01 joerg Exp $
# Original from FreeBSD, no rcs id.

LIBISPRIVATE=	yes

LIB=		install
SRCS=		automatic.c conflicts.c dewey.c \
		fexec.c file.c global.c gpgsig.c iterate.c license.c lpkg.c \
		opattern.c parse-config.c pkg_signature.c \
		pkgdb.c pkg_io.c plist.c remove.c \
		str.c version.c var.c vulnerabilities-file.c xwrapper.c

MAN=		pkg_install.conf.5 pkg_summary.5

version.o: version.h version.c

.include <bsd.init.mk>

# The PKCS7 code is only called under HAVE_SSL and only links with
# libcrypto; without either it is dead weight that does not link.
.if ${MKCRYPTO} != "no"
SRCS+=		pkcs7.c
.endif

# GCC 15 on a 2013 import, one named diagnostic per file:
#  - calloc with sizeof in the first argument (GCC 14 wants it second);
#  - readlink() into the buffer that holds its own argument -- the
#    restrict on the prototype is formally violated, the result is what
#    the code expects;
#  - a PKCS7 marker string that only the HAVE_SSL branch reads.
COPTS.license.c+=		${CC_WNO_CALLOC_TRANSPOSED_ARGS}
COPTS.plist.c+=			${CC_WNO_RESTRICT}
COPTS.vulnerabilities-file.c+=	${CC_WNO_UNUSED_CONST_VARIABLE}

pkg_install.conf.5: ${DIST}/lib/pkg_install.conf.5.in
	${TOOL_SED} -e 's,@SYSCONFDIR@,/etc,' \
	    ${DIST}/lib/pkg_install.conf.5.in > ${.TARGET}

.PATH:	${DIST}/lib

CLEANFILES+=	pkg_install.conf.5

CPPFLAGS+=	-DNETBSD

.include <bsd.lib.mk>
