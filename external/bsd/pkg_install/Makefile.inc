# $NetBSD: Makefile.inc,v 1.4 2011/09/16 16:41:20 joerg Exp $

.include <bsd.own.mk>

DIST=	${NETBSDSRCDIR}/external/bsd/pkg_install/dist

USE_FORT?=yes	# network client

CPPFLAGS+=-I${DIST}/lib
CPPFLAGS+=-I${NETBSDSRCDIR}/external/bsd/pkg_install/lib
CPPFLAGS+=-DHAVE_CONFIG_H -DNETBSD
# Without libcrypto (MKCRYPTO=no) pkg_install builds its plain variant: no
# package signatures, no pkg_admin audit.  Everything that installs,
# lists and removes packages is unchanged.
.if ${MKCRYPTO} != "no"
CPPFLAGS+=-DHAVE_SSL
.endif
.if !defined(__MINIX)
CPPFLAGS+=-DSYSCONFDIR='"/etc"'
.else
CPPFLAGS+=-DSYSCONFDIR='"/usr/pkg/etc"'
CPPFLAGS+=-DDEF_LOG_DIR='"/usr/pkg/var/db/pkg"'
CPPFLAGS+=-DOPSYS_NAME='"Minix"'
.endif # !defined(__MINIX)

WARNS=	4
CWARNFLAGS+=	-Wno-missing-noreturn
