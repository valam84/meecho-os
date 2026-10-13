# yambar 1.11.0 (stage 11.7: the panel of the desktop), reachover over the
# import in dist/ (port/gfx/import-stack.sh).

.if !defined(_YAMBAR_MAKEFILE_INC_)
_YAMBAR_MAKEFILE_INC_=	1

.include <bsd.own.mk>

YAMBARDIST=		${NETBSDSRCDIR}/external/mit/yambar/dist
WLPROTOCOLSDIST=	${NETBSDSRCDIR}/external/mit/wayland-protocols/dist

.endif
