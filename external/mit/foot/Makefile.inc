# foot 1.28.0 (stage 11.4: the terminal of the desktop), reachover over the
# import in dist/ (port/gfx/import-stack.sh).

.if !defined(_FOOT_MAKEFILE_INC_)
_FOOT_MAKEFILE_INC_=	1

.include <bsd.own.mk>

FOOTDIST=		${NETBSDSRCDIR}/external/mit/foot/dist
WLPROTOCOLSDIST=	${NETBSDSRCDIR}/external/mit/wayland-protocols/dist

.endif
