# fuzzel 1.15.0 (stage 11.7: the launcher and the command palette of the
# desktop), reachover over the import in dist/ (port/gfx/import-stack.sh).

.if !defined(_FUZZEL_MAKEFILE_INC_)
_FUZZEL_MAKEFILE_INC_=	1

.include <bsd.own.mk>

FUZZELDIST=		${NETBSDSRCDIR}/external/mit/fuzzel/dist
WLPROTOCOLSDIST=	${NETBSDSRCDIR}/external/mit/wayland-protocols/dist

.endif
