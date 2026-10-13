# FreeType 2.15.0 (stage 11: fcft rasterizes foot's text with it), used
# under the FreeType Licence (docs/FTL.TXT); reachover over the import in
# dist/ (port/gfx/import-stack.sh).

.if !defined(_FREETYPE_MAKEFILE_INC_)
_FREETYPE_MAKEFILE_INC_=	1

.include <bsd.own.mk>

FREETYPEDIST=	${NETBSDSRCDIR}/external/mit/freetype/dist

.endif
