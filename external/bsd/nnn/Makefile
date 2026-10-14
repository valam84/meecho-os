# nnn 5.3 (stage 11.7): the desktop's file manager - in the terminal, so
# it takes MEECHO's colours from foot's palette (meecho-desktop sets
# NNN_COLORS and NNN_FCOLORS to the sixteen ANSI ones).  Reachover over the
# import in dist/ (port/gfx/import-stack.sh), with the switches of nnn's
# own Makefile:
#
#   NORL	no readline (not in the tree)
#   NOMOUSE	NetBSD's curses has no mouse
#   NOX11	no X11: no xsel/xclip, no terminal title through X
#
# The rest as upstream builds by default: locale, the batch renamer, the
# FIFO for previews, sessions, user and group names, dimmed filter
# matches.  Its threads (disk usage, a thread per core) are libgfxcompat's
# pthreads over libmthread: they take turns, and the count is still right.

.include <bsd.own.mk>

NNNDIST=	${.CURDIR}/dist

PROG=		nnn
BINDIR=		/usr/bin
.PATH:		${NNNDIST}/src ${NNNDIST}

SRCS=		nnn.c
MAN=		nnn.1

CPPFLAGS+=	-DNORL -DNOMOUSE -DNOX11 -DDIM_FILTERED
CPPFLAGS+=	-D_NETBSD_SOURCE
CPPFLAGS+=	-I${DESTDIR}/usr/include/gfxcompat
CSTD=		c11
COPTS+=		-Wno-unused-parameter
COPTS+=		-fmacro-prefix-map=${NNNDIST}/=

LDADD+=	-lcurses -lterminfo -lgfxcompat -lmthread
DPADD+=	${LIBCURSES} ${LIBTERMINFO} ${DESTDIR}/usr/lib/libgfxcompat.a \
	${LIBMTHREAD}

.include <bsd.prog.mk>
