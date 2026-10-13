LIB=		wayland-server

SRCS=		wayland-server.c wayland-shm.c event-loop.c wayland-protocol.c
SRCS+=		wayland-util.c connection.c wayland-os.c

DPSRCS+=	wayland-version.h wayland-server-protocol.h \
		wayland-server-protocol-core.h
CLEANFILES+=	${DPSRCS} wayland-protocol.c

INCS=		wayland-server.h wayland-server-core.h \
		wayland-server-protocol.h
INCSDIR=	/usr/include

.include <bsd.lib.mk>
