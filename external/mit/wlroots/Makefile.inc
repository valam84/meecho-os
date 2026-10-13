# wlroots 0.20.2 (stage 11: the compositor library meechowm is built on),
# reachover over the import in dist/ (port/gfx/import-stack.sh).

.if !defined(_WLROOTS_MAKEFILE_INC_)
_WLROOTS_MAKEFILE_INC_=	1

.include <bsd.own.mk>

WLRDIST=		${NETBSDSRCDIR}/external/mit/wlroots/dist
WLPROTOCOLSDIST=	${NETBSDSRCDIR}/external/mit/wayland-protocols/dist

.endif
