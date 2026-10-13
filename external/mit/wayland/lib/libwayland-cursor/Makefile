# Cursor themes for clients (foot): Xcursor files, or the built-in set.

.include <bsd.init.mk>

LIB=		wayland-cursor

.PATH:		${WAYLANDDIST}/cursor
SRCS=		wayland-cursor.c os-compatibility.c xcursor.c

CPPFLAGS+=	-I${WAYLANDDIST}/cursor

INCS=		wayland-cursor.h
INCSDIR=	/usr/include

.include <bsd.lib.mk>
