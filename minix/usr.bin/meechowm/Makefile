# meechowm: the compositor of stage 11.4, with the strip and the layer
# surfaces of 11.7 (meechowm.h and wm.h say what is whose).  Built on the
# Wayland stack in external/mit; static, as the userland is.

PROG=	meechowm
SRCS=	meechowm.c strip.c layers.c layout.c ipc.c fbout.c input.c \
	mousesplit.c hid2evdev.c passport.c
MAN=
# The passport's font: Spleen 8x16, the screen console's (sys/dev/wsfont).
CPPFLAGS.passport.c+=	-I${NETBSDSRCDIR}/sys
# Super+d: the launcher and command palette, over fuzzel; the panel, over
# yambar (both external/mit); Print: the screenshot, over grim; and the
# desktop as a whole, from the console (stage 11.7).
SCRIPTS=	meechowm-palette.sh meechowm-bar.sh meechowm-shot.sh \
		meecho-desktop.sh

# MEECHO's colours, from its palette (minix/share/style): the ground and
# the focus ring (strip.c).
STYLEDIR=	${NETBSDSRCDIR}/minix/share/style
style.h: ${STYLEDIR}/palette ${STYLEDIR}/style.awk
	${_MKTARGET_CREATE}
	${TOOL_AWK} -v out=c -f ${STYLEDIR}/style.awk ${STYLEDIR}/palette \
	    > ${.TARGET}.tmp && mv ${.TARGET}.tmp ${.TARGET}
DPSRCS+=	style.h
CLEANFILES+=	style.h style.h.tmp

WLPROTOCOLSDIST=	${NETBSDSRCDIR}/external/mit/wayland-protocols/dist
WLROOTSPROTO=		${NETBSDSRCDIR}/external/mit/wlroots/dist/protocol

# wlroots' public headers ask for the protocol headers, and leave making
# them to the compositor - as tinywl does.
xdg-shell-protocol.h: ${WLPROTOCOLSDIST}/stable/xdg-shell/xdg-shell.xml
	${_MKTARGET_CREATE}
	${TOOL_WAYLAND_SCANNER} server-header ${.ALLSRC} ${.TARGET}
wlr-layer-shell-unstable-v1-protocol.h: \
    ${WLROOTSPROTO}/wlr-layer-shell-unstable-v1.xml
	${_MKTARGET_CREATE}
	${TOOL_WAYLAND_SCANNER} server-header ${.ALLSRC} ${.TARGET}
DPSRCS+=	xdg-shell-protocol.h wlr-layer-shell-unstable-v1-protocol.h
CLEANFILES+=	xdg-shell-protocol.h wlr-layer-shell-unstable-v1-protocol.h

CPPFLAGS+=	-DWLR_USE_UNSTABLE -D_NETBSD_SOURCE -I.
CPPFLAGS+=	-I${DESTDIR}/usr/include/wlroots-0.20 \
		-I${DESTDIR}/usr/include/pixman-1 \
		-I${DESTDIR}/usr/include/libdrm \
		-I${DESTDIR}/usr/include/gfxcompat
# FBIOC_MAP and FBIOC_FLIP: the frame buffer driver's own requests, in its
# arch header, which is not installed.
CPPFLAGS+=	-I${NETBSDSRCDIR}/minix/drivers/video/fb/arch/aarch64

# wlr_log() names the source file: by its name here, not the build host's
# path to it.
COPTS+=		-fmacro-prefix-map=${.CURDIR}/=

LDADD+=	-lwlroots-0.20 -lwayland-server -lwayland-client -lxkbcommon \
	-lpixman-1 -ldrm -lepoll-shim -lffi -lgfxcompat -lmthread -lm
DPADD+=	${DESTDIR}/usr/lib/libwlroots-0.20.a \
	${DESTDIR}/usr/lib/libwayland-server.a \
	${DESTDIR}/usr/lib/libwayland-client.a \
	${DESTDIR}/usr/lib/libxkbcommon.a ${DESTDIR}/usr/lib/libpixman-1.a \
	${DESTDIR}/usr/lib/libdrm.a ${DESTDIR}/usr/lib/libepoll-shim.a \
	${DESTDIR}/usr/lib/libffi.a ${DESTDIR}/usr/lib/libgfxcompat.a \
	${LIBMTHREAD} ${LIBM}

WARNS?=	3

.include <bsd.prog.mk>
