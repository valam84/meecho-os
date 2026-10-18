# meechowm-osk: the desktop's on-screen keyboard (stage 11.9) - libosk's
# layers and touches on a layer surface of meechowm, its keys through a
# virtual keyboard.  On the Wayland stack and fcft in external/mit; static,
# as the userland is.

PROG=	meechowm-osk
SRCS=	osk.c draw.c hid2evdev.c xdg-shell-protocol.c \
	wlr-layer-shell-protocol.c virtual-keyboard-protocol.c \
	input-method-protocol.c
MAN=

# HID usages to evdev codes: the compositor's own table, so that a key on
# the screen is the code a USB keyboard's would be.
.PATH:	${.CURDIR}/../meechowm

# MEECHO's colours, from its palette (minix/share/style), as meechowm
# takes them.
STYLEDIR=	${NETBSDSRCDIR}/minix/share/style
style.h: ${STYLEDIR}/palette ${STYLEDIR}/style.awk
	${_MKTARGET_CREATE}
	${TOOL_AWK} -v out=c -f ${STYLEDIR}/style.awk ${STYLEDIR}/palette \
	    > ${.TARGET}.tmp && mv ${.TARGET}.tmp ${.TARGET}

WLPROTOCOLSDIST=	${NETBSDSRCDIR}/external/mit/wayland-protocols/dist
WLROOTSPROTO=		${NETBSDSRCDIR}/external/mit/wlroots/dist/protocol
XDG_SHELL_XML=		${WLPROTOCOLSDIST}/stable/xdg-shell/xdg-shell.xml
LAYER_SHELL_XML=	${WLROOTSPROTO}/wlr-layer-shell-unstable-v1.xml
VKBD_XML=		${WLROOTSPROTO}/virtual-keyboard-unstable-v1.xml
IM_XML=			${WLROOTSPROTO}/input-method-unstable-v2.xml

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
virtual-keyboard-unstable-v1-client-protocol.h: ${VKBD_XML}
	${_MKTARGET_CREATE}
	${TOOL_WAYLAND_SCANNER} client-header ${.ALLSRC} ${.TARGET}
virtual-keyboard-protocol.c: ${VKBD_XML}
	${_MKTARGET_CREATE}
	${TOOL_WAYLAND_SCANNER} private-code ${.ALLSRC} ${.TARGET}
input-method-unstable-v2-client-protocol.h: ${IM_XML}
	${_MKTARGET_CREATE}
	${TOOL_WAYLAND_SCANNER} client-header ${.ALLSRC} ${.TARGET}
input-method-protocol.c: ${IM_XML}
	${_MKTARGET_CREATE}
	${TOOL_WAYLAND_SCANNER} private-code ${.ALLSRC} ${.TARGET}

DPSRCS+=	style.h wlr-layer-shell-unstable-v1-client-protocol.h \
		virtual-keyboard-unstable-v1-client-protocol.h \
		input-method-unstable-v2-client-protocol.h
CLEANFILES+=	style.h style.h.tmp xdg-shell-protocol.c \
		wlr-layer-shell-unstable-v1-client-protocol.h \
		wlr-layer-shell-protocol.c \
		virtual-keyboard-unstable-v1-client-protocol.h \
		virtual-keyboard-protocol.c \
		input-method-unstable-v2-client-protocol.h \
		input-method-protocol.c

CPPFLAGS+=	-D_NETBSD_SOURCE -I.
CPPFLAGS+=	-I${DESTDIR}/usr/include/pixman-1 \
		-I${DESTDIR}/usr/include/freetype2 \
		-I${DESTDIR}/usr/include/gfxcompat
# The scanner's client headers hand their const listener tables to
# wl_proxy_add_listener(), which takes them without const.
COPTS.osk.c+=	-Wno-cast-qual

LDADD+=	-losk -lxkbcommon -lfcft -lfontconfig -lfreetype -lexpat -lz \
	-lpixman-1 -lwayland-client -lepoll-shim -lffi -lgfxcompat \
	-lmthread -lm
DPADD+=	${DESTDIR}/usr/lib/libosk.a ${DESTDIR}/usr/lib/libxkbcommon.a \
	${DESTDIR}/usr/lib/libfcft.a ${DESTDIR}/usr/lib/libfontconfig.a \
	${DESTDIR}/usr/lib/libfreetype.a ${LIBEXPAT} ${LIBZ} \
	${DESTDIR}/usr/lib/libpixman-1.a \
	${DESTDIR}/usr/lib/libwayland-client.a \
	${DESTDIR}/usr/lib/libepoll-shim.a ${DESTDIR}/usr/lib/libffi.a \
	${DESTDIR}/usr/lib/libgfxcompat.a ${LIBMTHREAD} ${LIBM}

WARNS?=	3

.include <bsd.prog.mk>
