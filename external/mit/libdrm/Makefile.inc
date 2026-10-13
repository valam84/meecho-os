# libdrm 2.4.134 (stage 11: wlroots names pixel formats and modifiers with
# it), reachover over NetBSD's xsrc dist in dist/
# (port/gfx/import-stack.sh).  The core library only: there is no DRM
# device here, and no driver library is built.

.if !defined(_LIBDRM_MAKEFILE_INC_)
_LIBDRM_MAKEFILE_INC_=	1

.include <bsd.own.mk>

LIBDRMDIST=	${NETBSDSRCDIR}/external/mit/libdrm/dist

.endif
