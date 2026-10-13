# The Wayland stack's own test client (stage 11): port/gfx/test runs it
# under meechowm on QEMU and on the board.

.include <bsd.own.mk>

PROG=	wlhello
MAN=
BINDIR=	/usr/tests/gfx

WLPROTOCOLSDIST=	${NETBSDSRCDIR}/external/mit/wayland-protocols/dist
XDG_SHELL_XML=		${WLPROTOCOLSDIST}/stable/xdg-shell/xdg-shell.xml

xdg-shell-client-protocol.h: ${XDG_SHELL_XML}
	${_MKTARGET_CREATE}
	${TOOL_WAYLAND_SCANNER} client-header ${.ALLSRC} ${.TARGET}
xdg-shell-protocol.c: ${XDG_SHELL_XML}
	${_MKTARGET_CREATE}
	${TOOL_WAYLAND_SCANNER} private-code ${.ALLSRC} ${.TARGET}

SRCS=		wlhello.c xdg-shell-protocol.c
DPSRCS+=	xdg-shell-client-protocol.h
CLEANFILES+=	xdg-shell-client-protocol.h xdg-shell-protocol.c

CPPFLAGS+=	-D_NETBSD_SOURCE -I.
# The scanner's client header hands its const listener table to
# wl_proxy_add_listener(), which takes it without const.
COPTS.wlhello.c+=	-Wno-cast-qual

LDADD+=	-lwayland-client -lepoll-shim -lffi -lgfxcompat -lmthread -lm
DPADD+=	${DESTDIR}/usr/lib/libwayland-client.a \
	${DESTDIR}/usr/lib/libepoll-shim.a ${DESTDIR}/usr/lib/libffi.a \
	${DESTDIR}/usr/lib/libgfxcompat.a ${LIBMTHREAD} ${LIBM}

WARNS?=	3

.include <bsd.prog.mk>
