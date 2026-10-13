# libxkbcommon 1.13.2 (stage 11: keymaps for wlroots, meechowm and foot),
# reachover over the import in dist/ (port/gfx/import-stack.sh).

.if !defined(_XKBCOMMON_MAKEFILE_INC_)
_XKBCOMMON_MAKEFILE_INC_=	1

.include <bsd.own.mk>

XKBCOMMONDIST=	${NETBSDSRCDIR}/external/mit/libxkbcommon/dist

.endif
