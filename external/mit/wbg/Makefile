# wbg 1.3.0 (stage 11.7): the desktop's wallpaper - a layer-shell surface
# on the background layer of every output, an image scaled to cover it.
# Reachover over the import in dist/ (port/gfx/import-stack.sh), as
# meson builds it here: PNG (external/mit/libpng) and SVG (the bundled
# nanosvg, which draws SVG at the screen's own size) - the JPEG, WebP and
# JPEG XL readers are not imported.  meecho-desktop starts it with MEECHO's
# wallpaper, /usr/share/meecho/wallpaper.svg (minix/share/style).
#
# Beside this file: version.h, what wbg's generate-version.sh writes for
# the release.

.include <bsd.own.mk>

WBGDIST=		${.CURDIR}/dist
WLPROTOCOLSDIST=	${NETBSDSRCDIR}/external/mit/wayland-protocols/dist

PROG=		wbg
MAN=
BINDIR=		/usr/bin

.PATH:		${WBGDIST}

SRCS=		main.c log.c shm.c png.c svg.c nanosvg.c nanosvgrast.c

# Layer-shell is wbg's own copy; xdg-shell, which it names, is
# wayland-protocols'.
wlr-layer-shell-unstable-v1.h: \
    ${WBGDIST}/external/wlr-layer-shell-unstable-v1.xml
	${_MKTARGET_CREATE}
	${TOOL_WAYLAND_SCANNER} client-header ${.ALLSRC} ${.TARGET}
wlr-layer-shell-unstable-v1.c: \
    ${WBGDIST}/external/wlr-layer-shell-unstable-v1.xml
	${_MKTARGET_CREATE}
	${TOOL_WAYLAND_SCANNER} private-code ${.ALLSRC} ${.TARGET}
xdg-shell.h: ${WLPROTOCOLSDIST}/stable/xdg-shell/xdg-shell.xml
	${_MKTARGET_CREATE}
	${TOOL_WAYLAND_SCANNER} client-header ${.ALLSRC} ${.TARGET}
xdg-shell.c: ${WLPROTOCOLSDIST}/stable/xdg-shell/xdg-shell.xml
	${_MKTARGET_CREATE}
	${TOOL_WAYLAND_SCANNER} private-code ${.ALLSRC} ${.TARGET}
SRCS+=		wlr-layer-shell-unstable-v1.c xdg-shell.c
DPSRCS+=	wlr-layer-shell-unstable-v1.h xdg-shell.h
CLEANFILES+=	wlr-layer-shell-unstable-v1.c wlr-layer-shell-unstable-v1.h \
		xdg-shell.c xdg-shell.h

# What meson.build defines, and the two image formats built here.
CPPFLAGS+=	-D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE -D_NETBSD_SOURCE \
		-DNDEBUG -DWBG_HAVE_PNG=1 -DWBG_HAVE_SVG=1
CPPFLAGS+=	-I. -I${.CURDIR} -I${WBGDIST}
CPPFLAGS+=	-I${DESTDIR}/usr/include/pixman-1 \
		-I${DESTDIR}/usr/include/libepoll-shim \
		-I${DESTDIR}/usr/include/gfxcompat
CSTD=		c18
COPTS+=		-fmacro-prefix-map=${WBGDIST}/=

LDADD+=	-lpng -lz -lpixman-1 -lwayland-client -lepoll-shim -lffi \
	-lgfxcompat -lmthread -lm
DPADD+=	${DESTDIR}/usr/lib/libpng.a ${LIBZ} \
	${DESTDIR}/usr/lib/libpixman-1.a \
	${DESTDIR}/usr/lib/libwayland-client.a \
	${DESTDIR}/usr/lib/libepoll-shim.a ${DESTDIR}/usr/lib/libffi.a \
	${DESTDIR}/usr/lib/libgfxcompat.a ${LIBMTHREAD} ${LIBM}

.include <bsd.prog.mk>
