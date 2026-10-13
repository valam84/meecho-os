# fontconfig 2.18.3 (stage 11: fcft finds foot's fonts with it), reachover
# over the import in dist/ (port/gfx/import-stack.sh), after NetBSD's
# (src/external/mit/xorg/lib/fontconfig).
#
# include/ holds what fontconfig's build generates with tools the tree has
# not got - python for the case and language tables, gperf for the object
# hash - taken from the prototype's meson build of the same tag, as
# NetBSD's xsrc keeps them beside its dist; config.h there is MEECHO's.

.if !defined(_FONTCONFIG_MAKEFILE_INC_)
_FONTCONFIG_MAKEFILE_INC_=	1

.include <bsd.own.mk>

FONTCONFIGDIR=		${NETBSDSRCDIR}/external/mit/fontconfig
FONTCONFIGDIST=		${FONTCONFIGDIR}/dist
FONTCONFIGINCLUDE=	${FONTCONFIGDIR}/include

CPPFLAGS+=	-DHAVE_CONFIG_H -D_NETBSD_SOURCE
CPPFLAGS+=	-I${FONTCONFIGINCLUDE} -I${FONTCONFIGINCLUDE}/src \
		-I${FONTCONFIGINCLUDE}/fc-lang -I${FONTCONFIGINCLUDE}/fc-const \
		-I${FONTCONFIGINCLUDE}/fc-genericfamily \
		-I${FONTCONFIGDIST} -I${FONTCONFIGDIST}/src
CPPFLAGS+=	-I${DESTDIR}/usr/include/freetype2 \
		-I${DESTDIR}/usr/include/gfxcompat
COPTS+=		-std=c11

.endif
