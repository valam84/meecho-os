# libEGL: Mesa's EGL, its DRI2 driver over libgallium (linked directly, no
# loading), static.  Platforms: surfaceless and the DRM device; the headers
# of EGL and Khronos' platform types go with it.

.include <bsd.init.mk>

LIB=		EGL

.include "srcs.mk"

.PATH:		${MESADIST}/include
INCSDIR=	/usr/include
INCS=		EGL/egl.h EGL/eglext.h EGL/eglext_angle.h EGL/eglmesaext.h \
		EGL/eglplatform.h KHR/khrplatform.h

.include <bsd.lib.mk>
