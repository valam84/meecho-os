# $NetBSD: Makefile,v 1.2 2009/02/02 20:47:21 joerg Exp $
# Original from FreeBSD, no rcs id.

PROG= pkg_create
SRCS= main.c perform.c pl.c util.c build.c

.include <bsd.init.mk>

.PATH:	${DIST}/create

# GCC 7+: a getopt case that falls through on purpose, without the
# comment the compiler looks for.  Imported code, one diagnostic.
COPTS.main.c+=	${CC_WNO_IMPLICIT_FALLTHROUGH}

.include <bsd.prog.mk>
