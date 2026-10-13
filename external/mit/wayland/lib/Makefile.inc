.include "${.PARSEDIR}/../Makefile.inc"

# Each library carries its own copy of the code both sides share - the
# utilities, the marshalling, the OS wrappers and the core protocol's
# interfaces - as meson's static wayland-util and wayland-private do.  A
# program that links both takes each object once, from the first archive.
.PATH:	${WAYLANDDIST}/src

CPPFLAGS+=	-D_POSIX_C_SOURCE=200809L -D_NETBSD_SOURCE
# config.h is MEECHO's, beside this file: "config.h" from the sources'
# directory resolves to it through ${.CURDIR}/.., and "../config.h" (what
# connection.c and wayland-os.c say) through ${.CURDIR}.
CPPFLAGS+=	-I. -I${.CURDIR} -I${.CURDIR}/.. -I${WAYLANDDIST}/src
# epoll, eventfd, timerfd and signalfd come from libepoll-shim, over
# poll(2) (stage 11.3); its headers redirect read/write/close/poll too.
# <pthread.h> is libgfxcompat's: libmthread under the POSIX names.
CPPFLAGS+=	-I${DESTDIR}/usr/include/libepoll-shim \
		-I${DESTDIR}/usr/include/gfxcompat

WAYLAND_XML=	${WAYLANDDIST}/protocol/wayland.xml

wayland-protocol.c: ${WAYLAND_XML}
	${_MKTARGET_CREATE}
	${TOOL_WAYLAND_SCANNER} -s public-code ${.ALLSRC} ${.TARGET}

.for _side in client server
wayland-${_side}-protocol.h: ${WAYLAND_XML}
	${_MKTARGET_CREATE}
	${TOOL_WAYLAND_SCANNER} -s ${_side}-header ${.ALLSRC} ${.TARGET}

wayland-${_side}-protocol-core.h: ${WAYLAND_XML}
	${_MKTARGET_CREATE}
	${TOOL_WAYLAND_SCANNER} -s ${_side}-header -c ${.ALLSRC} ${.TARGET}
.endfor
