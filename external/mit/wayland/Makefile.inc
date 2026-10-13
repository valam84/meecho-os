# Wayland 1.26.0 (stage 11): the libraries, the scanner and the core
# protocol, reachover over the import in dist/ (port/gfx/import-stack.sh).

.if !defined(_WAYLAND_MAKEFILE_INC_)
_WAYLAND_MAKEFILE_INC_=	1

.include <bsd.own.mk>

WAYLAND_VERSION_MAJOR=	1
WAYLAND_VERSION_MINOR=	26
WAYLAND_VERSION_MICRO=	0
WAYLAND_VERSION=	${WAYLAND_VERSION_MAJOR}.${WAYLAND_VERSION_MINOR}.${WAYLAND_VERSION_MICRO}

WAYLANDDIR=	${NETBSDSRCDIR}/external/mit/wayland
WAYLANDDIST=	${WAYLANDDIR}/dist

# The version header, as meson makes it from its template.
wayland-version.h: ${WAYLANDDIST}/src/wayland-version.h.in
	${_MKTARGET_CREATE}
	${TOOL_SED} -e 's/@WAYLAND_VERSION@/${WAYLAND_VERSION}/' \
	    -e 's/@WAYLAND_VERSION_MAJOR@/${WAYLAND_VERSION_MAJOR}/' \
	    -e 's/@WAYLAND_VERSION_MINOR@/${WAYLAND_VERSION_MINOR}/' \
	    -e 's/@WAYLAND_VERSION_MICRO@/${WAYLAND_VERSION_MICRO}/' \
	    < ${.ALLSRC} > ${.TARGET}

.endif
