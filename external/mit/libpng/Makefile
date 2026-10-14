# libpng 1.6.59 (stage 11.7): PNG for the desktop - grim writes its
# screenshots with it, wbg reads wallpapers with it.  Reachover over the
# import in dist/ (port/gfx/import-stack.sh); the libpng licence
# (dist/LICENSE) is permissive.  In external/mit with the rest of the
# desktop's stack, as FreeType is.
#
# The configuration is upstream's ready-made one, scripts/pnglibconf.h.
# prebuilt (every feature the library has; pngusr.dfa and awk are how one
# would cut it down).  On AArch64 the NEON filters switch themselves on
# (pngpriv.h: __ARM_NEON), and arm_init.c asks the processor nothing - NEON
# is part of the architecture.

NOLINT=	1	# defined

.include <bsd.own.mk>

LIBPNGDIST=	${.CURDIR}/dist
.PATH:	${LIBPNGDIST} ${LIBPNGDIST}/arm ${LIBPNGDIST}/scripts

LIB=		png
SRCS=		png.c pngerror.c pngget.c pngmem.c pngpread.c pngread.c \
		pngrio.c pngrtran.c pngrutil.c pngset.c pngtrans.c pngwio.c \
		pngwrite.c pngwtran.c pngwutil.c
SRCS+=		arm_init.c filter_neon_intrinsics.c palette_neon_intrinsics.c

# png.h includes pnglibconf.h: a copy here to build with, and the same
# file installed under its own name.
pnglibconf.h: pnglibconf.h.prebuilt
	${_MKTARGET_CREATE}
	cp ${.ALLSRC} ${.TARGET}
DPSRCS+=	pnglibconf.h
CLEANFILES+=	pnglibconf.h
CPPFLAGS+=	-I. -I${LIBPNGDIST}

INCS=		png.h pngconf.h pnglibconf.h.prebuilt
INCSNAME_pnglibconf.h.prebuilt=	pnglibconf.h
INCSDIR=	/usr/include

.include <bsd.lib.mk>
