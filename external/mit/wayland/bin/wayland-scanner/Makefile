# wayland-scanner: protocol XML to C.  Built twice - for the machine here,
# and for the build host by tools/wayland-scanner (nbwayland-scanner),
# which is the one every reachover of the stack runs.

.include <bsd.init.mk>

PROG=		wayland-scanner
MAN=

.PATH:		${WAYLANDDIST}/src
SRCS=		scanner.c wayland-util.c

DPSRCS+=	wayland-version.h
CLEANFILES+=	wayland-version.h

# meson gives it -include config.h, for this one answer.
CPPFLAGS+=	-DHAVE_STRNDUP -I. -I${WAYLANDDIST}/src

# On the host the tree's expat is compiled in (tools/wayland-scanner).
.if !defined(HOSTPROGNAME)
LDADD+=		-lexpat
DPADD+=		${DESTDIR}/usr/lib/libexpat.a
.endif

.include <bsd.prog.mk>
