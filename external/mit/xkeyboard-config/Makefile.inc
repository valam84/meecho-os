# xkeyboard-config 2.38 (stage 11: the keymaps libxkbcommon compiles), the
# data of NetBSD's xsrc in dist/ (port/gfx/import-stack.sh).

.if !defined(_XKBCONFIG_MAKEFILE_INC_)
_XKBCONFIG_MAKEFILE_INC_=	1

.include <bsd.own.mk>

XKBCONFIGDIST=	${NETBSDSRCDIR}/external/mit/xkeyboard-config/dist
XKBCONFIGDIR=	/usr/share/X11/xkb

.endif
