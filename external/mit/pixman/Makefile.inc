# pixman 0.46.4 (stage 11: wlroots renders with it, fcft and foot draw
# with it), reachover over NetBSD's xsrc dist in dist/
# (port/gfx/import-stack.sh).

.if !defined(_PIXMAN_MAKEFILE_INC_)
_PIXMAN_MAKEFILE_INC_=	1

.include <bsd.own.mk>

PIXMANDIST=	${NETBSDSRCDIR}/external/mit/pixman/dist

.endif
