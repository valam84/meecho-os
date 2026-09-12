# $NetBSD: Makefile,v 1.2 2009/02/25 21:23:17 joerg Exp $
# Original from FreeBSD, no rcs id.

PROG= pkg_delete

.include <bsd.init.mk>

.PATH:	${DIST}/delete

# GCC 7+: a getopt case that falls through on purpose, without the
# comment the compiler looks for.  Imported code, one diagnostic.
COPTS.pkg_delete.c+=	${CC_WNO_IMPLICIT_FALLTHROUGH}

.include <bsd.prog.mk>
