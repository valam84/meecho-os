# slurp 1.5.0 (stage 11.7): an area of the screen chosen with the mouse -
# a layer-shell overlay, drawn with cairo, that prints the rectangle as
# grim's -g wants it.  meechowm-shot -r (Shift+Print) is how the desktop
# calls it, with MEECHO's colours (/usr/share/meecho/slurp.sh, from
# minix/share/style).  Reachover over the import in dist/
# (port/gfx/import-stack.sh), as meson builds it here: without the manual
# (scdoc).

.include <bsd.own.mk>

SLURPDIST=		${.CURDIR}/dist
WLPROTOCOLSDIST=	${NETBSDSRCDIR}/external/mit/wayland-protocols/dist

PROG=		slurp
MAN=
BINDIR=		/usr/bin

.PATH:		${SLURPDIST}

SRCS=		main.c pool-buffer.c render.c

# The protocols, as slurp names them: NAME-client-protocol.h and
# NAME-protocol.c.  cursor-shape asks for tablet's interfaces, and
# layer-shell for xdg-shell's popup.
SLURP_PROTOCOLS= \
	${WLPROTOCOLSDIST}/stable/xdg-shell/xdg-shell.xml \
	${WLPROTOCOLSDIST}/staging/cursor-shape/cursor-shape-v1.xml \
	${WLPROTOCOLSDIST}/unstable/tablet/tablet-unstable-v2.xml \
	${WLPROTOCOLSDIST}/unstable/xdg-output/xdg-output-unstable-v1.xml \
	${SLURPDIST}/protocol/wlr-layer-shell-unstable-v1.xml

.for _x in ${SLURP_PROTOCOLS}
${_x:T:R}-client-protocol.h: ${_x}
	${_MKTARGET_CREATE}
	${TOOL_WAYLAND_SCANNER} client-header ${.ALLSRC} ${.TARGET}
${_x:T:R}-protocol.c: ${_x}
	${_MKTARGET_CREATE}
	${TOOL_WAYLAND_SCANNER} private-code ${.ALLSRC} ${.TARGET}
SRCS+=		${_x:T:R}-protocol.c
DPSRCS+=	${_x:T:R}-client-protocol.h
CLEANFILES+=	${_x:T:R}-protocol.c ${_x:T:R}-client-protocol.h
.endfor

# _NETBSD_SOURCE: memfd_create(2), which pool-buffer.c uses here in place
# of shm_open(3).  <linux/input-event-codes.h> (BTN_LEFT) is
# libgfxcompat's.
CPPFLAGS+=	-D_NETBSD_SOURCE
CPPFLAGS+=	-I. -I${SLURPDIST}/include
CPPFLAGS+=	-I${DESTDIR}/usr/include/gfxcompat
CSTD=		c11
COPTS+=		-Wno-unused-parameter

LDADD+=	-lcairo -lpng -lz -lfontconfig -lexpat -lfreetype -lpixman-1 \
	-lwayland-cursor -lwayland-client -lxkbcommon -lepoll-shim -lffi \
	-lgfxcompat -lmthread -lm
DPADD+=	${DESTDIR}/usr/lib/libcairo.a ${DESTDIR}/usr/lib/libpng.a ${LIBZ} \
	${DESTDIR}/usr/lib/libfontconfig.a ${LIBEXPAT} \
	${DESTDIR}/usr/lib/libfreetype.a ${DESTDIR}/usr/lib/libpixman-1.a \
	${DESTDIR}/usr/lib/libwayland-cursor.a \
	${DESTDIR}/usr/lib/libwayland-client.a \
	${DESTDIR}/usr/lib/libxkbcommon.a ${DESTDIR}/usr/lib/libepoll-shim.a \
	${DESTDIR}/usr/lib/libffi.a ${DESTDIR}/usr/lib/libgfxcompat.a \
	${LIBMTHREAD} ${LIBM}

.include <bsd.prog.mk>
