# NetBSD's reachover (src/external/mit/xorg/lib/libdrm, Makefile and
# Makefile.defines), outside X11 and without the driver libraries.  The
# defines are what meson's checks found here for the prototype (port/gfx).
# generated_static_table_fourcc.h beside this file is NetBSD's, made by
# gen_table_fourcc.py from the same drm_fourcc.h.

.include <bsd.init.mk>

LIB=	drm
.PATH:	${LIBDRMDIST}

SRCS=	xf86drm.c xf86drmHash.c xf86drmMode.c xf86drmRandom.c xf86drmSL.c

INCS=	xf86drm.h xf86drmMode.h libsync.h
INCSDIR=/usr/include

SUBDIR=	drm

CPPFLAGS+=	-I${.CURDIR} -I${LIBDRMDIST} -I${LIBDRMDIST}/include/drm

CPPFLAGS+=	-D_NETBSD_SOURCE \
		-DHAVE_OPEN_MEMSTREAM=1 \
		-DHAVE_SYS_SELECT_H=1 \
		-DHAVE_SYS_SYSCTL_H=1 \
		-DHAVE_VISIBILITY=1 \
		-DHAVE_LIBDRM_ATOMIC_PRIMITIVES=1

COPTS+=		-fvisibility=hidden

# xf86drm.c says "#warning Missing implementation of ..." on a system it
# does not know - finding a DRM device's bus and PCI ids; there is no DRM
# device here to find.  NetBSD has the same line, for clang's spelling too.
COPTS.xf86drm.c+=	${${ACTIVE_CC} == "gcc":? -Wno-error=cpp :}

.include <bsd.lib.mk>
.include <bsd.subdir.mk>
