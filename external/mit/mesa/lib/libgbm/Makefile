# libgbm: Mesa's buffer allocator front, static.  EGL's DRM platform is
# built on it.  Its backend (dri_gbm) it loads with dlopen(), which a static
# program cannot: here gbm_create_device() finds none and says so, and EGL
# is used on its surfaceless platform instead.

.include <bsd.init.mk>

LIB=		gbm

.include "srcs.mk"

.PATH:		${MESADIST}/src/gbm/main
INCSDIR=	/usr/include
INCS=		gbm.h

.include <bsd.lib.mk>
