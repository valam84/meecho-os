# meechowm-notify: a card on the desktop for each restart of a system
# service (stage 11.7, notify.h).  A layer-shell client of
# meechowm, on the Wayland stack and fcft in external/mit; static, as the
# userland is.

PROG=	meechowm-notify
SRCS=	notify.c svc.c card.c xdg-shell-protocol.c wlr-layer-shell-protocol.c
MAN=

# MEECHO's colours, from its palette (minix/share/style), as meechowm
# takes them.
STYLEDIR=	${NETBSDSRCDIR}/minix/share/style
style.h: ${STYLEDIR}/palette ${STYLEDIR}/style.awk
	${_MKTARGET_CREATE}
	${TOOL_AWK} -v out=c -f ${STYLEDIR}/style.awk ${STYLEDIR}/palette \
	    > ${.TARGET}.tmp && mv ${.TARGET}.tmp ${.TARGET}

WLPROTOCOLSDIST=	${NETBSDSRCDIR}/external/mit/wayland-protocols/dist
XDG_SHELL_XML=		${WLPROTOCOLSDIST}/stable/xdg-shell/xdg-shell.xml
LAYER_SHELL_XML=	\
    ${NETBSDSRCDIR}/external/mit/wlroots/dist/protocol/wlr-layer-shell-unstable-v1.xml

# The layer shell's code names xdg_popup (get_popup): xdg-shell's code
# comes along, as in wlhello.
xdg-shell-protocol.c: ${XDG_SHELL_XML}
	${_MKTARGET_CREATE}
	${TOOL_WAYLAND_SCANNER} private-code ${.ALLSRC} ${.TARGET}
wlr-layer-shell-unstable-v1-client-protocol.h: ${LAYER_SHELL_XML}
	${_MKTARGET_CREATE}
	${TOOL_WAYLAND_SCANNER} client-header ${.ALLSRC} ${.TARGET}
wlr-layer-shell-protocol.c: ${LAYER_SHELL_XML}
	${_MKTARGET_CREATE}
	${TOOL_WAYLAND_SCANNER} private-code ${.ALLSRC} ${.TARGET}

DPSRCS+=	style.h wlr-layer-shell-unstable-v1-client-protocol.h
CLEANFILES+=	style.h style.h.tmp xdg-shell-protocol.c \
		wlr-layer-shell-unstable-v1-client-protocol.h \
		wlr-layer-shell-protocol.c

CPPFLAGS+=	-D_NETBSD_SOURCE -I.
CPPFLAGS+=	-I${DESTDIR}/usr/include/pixman-1 \
		-I${DESTDIR}/usr/include/freetype2 \
		-I${DESTDIR}/usr/include/gfxcompat
# The scanner's client header hands its const listener table to
# wl_proxy_add_listener(), which takes it without const.
COPTS.notify.c+=	-Wno-cast-qual

LDADD+=	-lfcft -lfontconfig -lfreetype -lexpat -lz -lpixman-1 \
	-lwayland-client -lepoll-shim -lffi -lgfxcompat -lmthread -lm
DPADD+=	${DESTDIR}/usr/lib/libfcft.a ${DESTDIR}/usr/lib/libfontconfig.a \
	${DESTDIR}/usr/lib/libfreetype.a ${LIBEXPAT} ${LIBZ} \
	${DESTDIR}/usr/lib/libpixman-1.a \
	${DESTDIR}/usr/lib/libwayland-client.a \
	${DESTDIR}/usr/lib/libepoll-shim.a ${DESTDIR}/usr/lib/libffi.a \
	${DESTDIR}/usr/lib/libgfxcompat.a ${LIBMTHREAD} ${LIBM}

WARNS?=	3

.include <bsd.prog.mk>
