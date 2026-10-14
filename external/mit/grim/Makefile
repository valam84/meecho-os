# grim 1.5.0 (stage 11.7): screenshots of the desktop - grim asks the
# compositor for the outputs' pixels (ext-image-copy-capture, or
# wlr-screencopy where that is all there is; meechowm offers both) and
# writes PNG (external/mit/libpng) or PPM.  Reachover over the import in
# dist/ (port/gfx/import-stack.sh), as meson builds it here: without JPEG,
# without the manual (scdoc).  meechowm-shot is how the desktop calls it.

.include <bsd.own.mk>

GRIMDIST=		${.CURDIR}/dist
WLPROTOCOLSDIST=	${NETBSDSRCDIR}/external/mit/wayland-protocols/dist

PROG=		grim
MAN=
BINDIR=		/usr/bin

.PATH:		${GRIMDIST}

SRCS=		box.c buffer.c main.c output-layout.c render.c write_ppm.c \
		write_png.c

# The protocols, as grim names them: NAME-protocol.h and NAME-protocol.c.
GRIM_PROTOCOLS= \
	${WLPROTOCOLSDIST}/staging/ext-foreign-toplevel-list/ext-foreign-toplevel-list-v1.xml \
	${WLPROTOCOLSDIST}/staging/ext-image-capture-source/ext-image-capture-source-v1.xml \
	${WLPROTOCOLSDIST}/staging/ext-image-copy-capture/ext-image-copy-capture-v1.xml \
	${WLPROTOCOLSDIST}/unstable/xdg-output/xdg-output-unstable-v1.xml \
	${GRIMDIST}/protocol/wlr-screencopy-unstable-v1.xml

.for _x in ${GRIM_PROTOCOLS}
${_x:T:R}-protocol.h: ${_x}
	${_MKTARGET_CREATE}
	${TOOL_WAYLAND_SCANNER} client-header ${.ALLSRC} ${.TARGET}
${_x:T:R}-protocol.c: ${_x}
	${_MKTARGET_CREATE}
	${TOOL_WAYLAND_SCANNER} private-code ${.ALLSRC} ${.TARGET}
SRCS+=		${_x:T:R}-protocol.c
DPSRCS+=	${_x:T:R}-protocol.h
CLEANFILES+=	${_x:T:R}-protocol.c ${_x:T:R}-protocol.h
.endfor

# What meson.build defines.  _NETBSD_SOURCE: memfd_create(2), which
# buffer.c uses here in place of shm_open(3).
CPPFLAGS+=	-D_POSIX_C_SOURCE=200809L -D_NETBSD_SOURCE \
		-DGRIM_LITTLE_ENDIAN=1 -DHAVE_JPEG=0
CPPFLAGS+=	-I. -I${GRIMDIST}/include
CPPFLAGS+=	-I${DESTDIR}/usr/include/pixman-1
CSTD=		c11
COPTS+=		-Wno-unused-parameter -Wundef
COPTS+=		-fmacro-prefix-map=${GRIMDIST}/=

LDADD+=	-lpng -lz -lpixman-1 -lwayland-client -lepoll-shim -lffi \
	-lgfxcompat -lmthread -lm
DPADD+=	${DESTDIR}/usr/lib/libpng.a ${LIBZ} \
	${DESTDIR}/usr/lib/libpixman-1.a \
	${DESTDIR}/usr/lib/libwayland-client.a \
	${DESTDIR}/usr/lib/libepoll-shim.a ${DESTDIR}/usr/lib/libffi.a \
	${DESTDIR}/usr/lib/libgfxcompat.a ${LIBMTHREAD} ${LIBM}

.include <bsd.prog.mk>
