LIB=		wayland-client

SRCS=		wayland-client.c wayland-protocol.c
SRCS+=		wayland-util.c connection.c wayland-os.c

DPSRCS+=	wayland-version.h wayland-client-protocol.h \
		wayland-client-protocol-core.h
CLEANFILES+=	${DPSRCS} wayland-protocol.c

INCS=		wayland-client.h wayland-client-core.h \
		wayland-client-protocol.h
INCSDIR=	/usr/include

.include <bsd.lib.mk>
