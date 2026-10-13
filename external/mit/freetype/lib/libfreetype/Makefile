# libfreetype, after NetBSD's reachover (src/external/mit/xorg/lib/freetype)
# with the module list of 2.15.0 - the one meson builds, and the one the
# dist's ftmodule.h names - and outside X11, in /usr/include/freetype2 and
# /usr/lib.
#
# As NetBSD has it: gzip-compressed fonts through the system's libz
# (FT_CONFIG_OPTION_SYSTEM_ZLIB; the prototype compiled FreeType's own
# copy).  As the prototype had it: no bzip2, PNG, HarfBuzz or Brotli, no
# sub-pixel rendering, and the Unix ftsystem.c, which maps a font file
# instead of reading it (the two answers of builds/unix/ftconfig.h are the
# -D below).

.include <bsd.init.mk>

LIB=	freetype

.PATH:	${FREETYPEDIST}/src/base
SRCS=	ftbase.c ftinit.c ftdebug.c \
	ftbbox.c ftbdf.c ftbitmap.c ftcid.c ftfstype.c ftgasp.c ftglyph.c \
	ftgxval.c ftmm.c ftotval.c ftpatent.c ftpfr.c ftstroke.c ftsynth.c \
	fttype1.c ftwinfnt.c

.PATH:	${FREETYPEDIST}/builds/unix
SRCS+=	ftsystem.c

.PATH:	${FREETYPEDIST}/src/dlg
SRCS+=	dlgwrap.c

.for _m _f in truetype truetype type1 type1 cff cff cid type1cid hvf hvf \
    pfr pfr type42 type42 winfonts winfnt pcf pcf bdf bdf sfnt sfnt \
    autofit autofit pshinter pshinter smooth smooth raster raster svg svg \
    sdf sdf cache ftcache gzip ftgzip lzw ftlzw psaux psaux psnames psnames
.PATH:	${FREETYPEDIST}/src/${_m}
SRCS+=	${_f}.c
.endfor

CPPFLAGS+=	-DFT2_BUILD_LIBRARY -DFT_CONFIG_OPTION_SYSTEM_ZLIB \
		-DHAVE_UNISTD_H -DHAVE_FCNTL_H
CPPFLAGS+=	-I${FREETYPEDIST}/include

.PATH:		${FREETYPEDIST}/include
INCSDIR=	/usr/include/freetype2
INCS=	ft2build.h

.for _h in freetype.h ftadvanc.h ftbbox.h ftbdf.h ftbitmap.h ftbzip2.h \
    ftcache.h ftchapters.h ftcid.h ftcolor.h ftdriver.h fterrdef.h \
    fterrors.h ftfntfmt.h ftgasp.h ftglyph.h ftgxval.h ftgzip.h ftimage.h \
    ftincrem.h ftlcdfil.h ftlist.h ftlogging.h ftlzw.h ftmac.h ftmm.h \
    ftmodapi.h ftmoderr.h ftotval.h ftoutln.h ftparams.h ftpfr.h ftrender.h \
    ftsizes.h ftsnames.h ftstroke.h ftsynth.h ftsystem.h fttrigon.h \
    fttypes.h ftwinfnt.h otsvg.h t1tables.h ttnameid.h tttables.h tttags.h
INCS+=	freetype/${_h}
.endfor

.for _h in ftconfig.h ftheader.h ftmodule.h ftoption.h ftstdlib.h \
    integer-types.h mac-support.h public-macros.h
INCS+=	freetype/config/${_h}
.endfor

.include <bsd.lib.mk>
