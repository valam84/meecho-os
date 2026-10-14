# libGLESv2: the entry points of OpenGL ES 2 and 3, over libgallium's
# dispatch, static; and their headers.

.include <bsd.init.mk>

LIB=		GLESv2

.include "srcs.mk"

.PATH:		${MESADIST}/include
INCSDIR=	/usr/include
INCS=		GLES2/gl2.h GLES2/gl2ext.h GLES2/gl2platform.h \
		GLES3/gl3.h GLES3/gl31.h GLES3/gl32.h GLES3/gl3ext.h \
		GLES3/gl3platform.h

.include <bsd.lib.mk>
