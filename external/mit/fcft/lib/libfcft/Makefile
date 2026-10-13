# libfcft, as meson builds it here: no text shaping (no HarfBuzz or
# utf8proc), no SVG glyphs (-Dsvg-backend=none).
#
# Beside this file: emoji-data.h and unicode-compose-table.h, made by
# fcft's generators (generate-emoji-data.py, generate-unicode-precompose.sh)
# from Unicode's data files, taken from the prototype's build of this tag
# (unicode/license.txt in dist/ is the data's licence); version.h by hand.

.include <bsd.init.mk>

LIB=		fcft

.PATH:		${FCFTDIST}
SRCS=		fcft.c log.c

CPPFLAGS+=	-D_GNU_SOURCE=200809L -D_NETBSD_SOURCE -DMEMFD_CREATE -DNDEBUG
# meson gives the exported symbols default visibility; in a static library
# there is nothing to export them from.
CPPFLAGS+=	-DFCFT_EXPORT=
CPPFLAGS+=	-I${.CURDIR} -I${FCFTDIST}
CPPFLAGS+=	-I${DESTDIR}/usr/include/freetype2 \
		-I${DESTDIR}/usr/include/pixman-1 \
		-I${DESTDIR}/usr/include/gfxcompat
COPTS+=		-std=c11
# Source paths in log messages, as fcft's meson.build has them: relative.
COPTS+=		-fmacro-prefix-map=${FCFTDIST}/=

.PATH:		${FCFTDIST}/fcft
INCS=		fcft.h stride.h
INCSDIR=	/usr/include/fcft

.include <bsd.lib.mk>
