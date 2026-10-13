# fuzzel, the launcher and the command palette of the desktop (stage
# 11.7), as meson builds it here: without icons (no libpng, no SVG back end,
# no cairo), docs not built (scdoc), fuzzel.ini not installed - the
# palette (meechowm-palette) gives its options on the command line.
#
# Beside this file: srgb.c and srgb.h, made by fuzzel's srgb.py with the
# build host's python3 at import; version.h by hand.

.include <bsd.init.mk>

PROG=		fuzzel
MAN=
BINDIR=		/usr/bin

.PATH:		${FUZZELDIST}

SRCS=		application.c char32.c clipboard.c column.c config.c debug.c \
		dmenu.c event.c fdm.c icon.c key-binding.c log.c main.c \
		match.c path.c png.c prompt.c render.c shm.c uri.c wayland.c \
		xdg.c xmalloc.c xsnprintf.c timing.c srgb.c

# The protocols, as fuzzel names them: client header NAME.h, code NAME.c.
# Layer-shell is its own copy; the rest are wayland-protocols'.
FUZZEL_PROTOCOLS= \
	stable/xdg-shell/xdg-shell.xml \
	unstable/xdg-output/xdg-output-unstable-v1.xml \
	staging/xdg-activation/xdg-activation-v1.xml \
	staging/cursor-shape/cursor-shape-v1.xml \
	unstable/tablet/tablet-unstable-v2.xml \
	stable/viewporter/viewporter.xml \
	staging/fractional-scale/fractional-scale-v1.xml \
	unstable/primary-selection/primary-selection-unstable-v1.xml \
	staging/color-management/color-management-v1.xml \
	staging/single-pixel-buffer/single-pixel-buffer-v1.xml \
	unstable/text-input/text-input-unstable-v3.xml

.for _x in ${FUZZEL_PROTOCOLS}
${_x:T:R}.h: ${WLPROTOCOLSDIST}/${_x}
	${_MKTARGET_CREATE}
	${TOOL_WAYLAND_SCANNER} client-header ${.ALLSRC} ${.TARGET}
${_x:T:R}.c: ${WLPROTOCOLSDIST}/${_x}
	${_MKTARGET_CREATE}
	${TOOL_WAYLAND_SCANNER} private-code ${.ALLSRC} ${.TARGET}
SRCS+=		${_x:T:R}.c
DPSRCS+=	${_x:T:R}.h
CLEANFILES+=	${_x:T:R}.c ${_x:T:R}.h
.endfor

wlr-layer-shell-unstable-v1.h: \
    ${FUZZELDIST}/external/wlr-layer-shell-unstable-v1.xml
	${_MKTARGET_CREATE}
	${TOOL_WAYLAND_SCANNER} client-header ${.ALLSRC} ${.TARGET}
wlr-layer-shell-unstable-v1.c: \
    ${FUZZELDIST}/external/wlr-layer-shell-unstable-v1.xml
	${_MKTARGET_CREATE}
	${TOOL_WAYLAND_SCANNER} private-code ${.ALLSRC} ${.TARGET}
SRCS+=		wlr-layer-shell-unstable-v1.c
DPSRCS+=	wlr-layer-shell-unstable-v1.h
CLEANFILES+=	wlr-layer-shell-unstable-v1.c wlr-layer-shell-unstable-v1.h

# What meson.build defines here: memfd_create(2) found, a release build.
# _OPENBSD_SOURCE: libc has reallocarray(3), and declares it only so.
CPPFLAGS+=	-D_GNU_SOURCE -D_NETBSD_SOURCE -D_OPENBSD_SOURCE -DNDEBUG \
		-DMEMFD_CREATE
CPPFLAGS+=	-I. -I${.CURDIR} -I${FUZZELDIST}
CPPFLAGS+=	-I${DESTDIR}/usr/include/pixman-1 \
		-I${DESTDIR}/usr/include/freetype2 \
		-I${DESTDIR}/usr/include/libepoll-shim \
		-I${DESTDIR}/usr/include/gfxcompat
# c18, as meson.build has it - through CSTD, which bsd.sys.mk puts last
# (-std in COPTS would come before its gnu99, and lose).
CSTD=		c18
COPTS+=		-fno-asynchronous-unwind-tables
COPTS+=		-fmacro-prefix-map=${FUZZELDIST}/=

LDADD+=	-lfcft -lfontconfig -lfreetype -lexpat -lz -lpixman-1 \
	-lwayland-cursor -lwayland-client -lxkbcommon -lepoll-shim -lffi \
	-lgfxcompat -lmthread -lm
DPADD+=	${DESTDIR}/usr/lib/libfcft.a ${DESTDIR}/usr/lib/libfontconfig.a \
	${DESTDIR}/usr/lib/libfreetype.a ${LIBEXPAT} ${LIBZ} \
	${DESTDIR}/usr/lib/libpixman-1.a ${DESTDIR}/usr/lib/libwayland-cursor.a \
	${DESTDIR}/usr/lib/libwayland-client.a ${DESTDIR}/usr/lib/libxkbcommon.a \
	${DESTDIR}/usr/lib/libepoll-shim.a ${DESTDIR}/usr/lib/libffi.a \
	${DESTDIR}/usr/lib/libgfxcompat.a ${LIBMTHREAD} ${LIBM}

.include <bsd.prog.mk>
