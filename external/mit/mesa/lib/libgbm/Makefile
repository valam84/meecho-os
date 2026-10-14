# libgbm: Mesa's buffer allocator front, static, with its DRI backend
# (gbm_dri.c over libgallium) linked in.  Mesa's own backend.c loads the
# backend with dlopen(), which a static program cannot: gbm_static.c takes
# its place and asks the built-in one.  wlroots' GBM allocator and EGL's
# GBM platform are built on this.

.include <bsd.init.mk>

LIB=		gbm

.include "srcs.mk"

SRCS:=		${SRCS:Ngbm-main-backend.c} gbm_static.c
CPPFLAGS.gbm_static.c+=	${MESA_CPPFLAGS} -I${MESADIST}/src/gbm/main

.PATH:		${MESADIST}/src/gbm/main
INCSDIR=	/usr/include
INCS=		gbm.h

.include <bsd.lib.mk>
