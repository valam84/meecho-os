# $NetBSD: Makefile,v 1.3 2011/05/26 12:56:26 joerg Exp $
# Original from FreeBSD, no rcs id

PROG= pkg_info
SRCS= main.c perform.c show.c

.include <bsd.init.mk>

.PATH:	${DIST}/info

CWARNFLAGS.clang+=	-Wno-format-security

# GCC 7+: a getopt case that falls through on purpose, without the
# comment the compiler looks for.  Imported code, one diagnostic.
COPTS.main.c+=	${CC_WNO_IMPLICIT_FALLTHROUGH}

.include <bsd.prog.mk>
