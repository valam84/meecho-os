# $NetBSD: Makefile,v 1.2 2009/02/02 20:47:21 joerg Exp $

PROG= pkg_admin
SRCS= audit.c check.c main.c

SCRIPTS=	audit-packages.sh download-vulnerability-list.sh
CLEANFILES=	audit-packages.sh download-vulnerability-list.sh

audit-packages.sh: audit-packages.sh.in
	sed -e 's|@PKG_ADMIN@|/usr/sbin/pkg_admin|' \
	    ${DIST}/admin/audit-packages.sh.in > $@

download-vulnerability-list.sh: download-vulnerability-list.sh.in
	sed -e 's|@PKG_ADMIN@|/usr/sbin/pkg_admin|' \
	    ${DIST}/admin/download-vulnerability-list.sh.in > $@

LINK_PKGVUL=	yes

.include <bsd.init.mk>

.PATH:	${DIST}/admin
# GCC 7+/12+ on imported code, two named diagnostics in one file: a getopt
# case that falls through on purpose, and errx() in add_required_by()
# printing a path the line before it freed -- a real slip in an error
# message, reached only when write(2) to +REQUIRED_BY fails after open(2)
# succeeded.  Left as imported; the fix belongs upstream.
COPTS.main.c+=	${CC_WNO_IMPLICIT_FALLTHROUGH} ${CC_WNO_USE_AFTER_FREE}

.include <bsd.prog.mk>
