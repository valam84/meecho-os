# fcft 3.3.3 (stage 11: foot's font loading and glyph rasterization),
# reachover over the import in dist/ (port/gfx/import-stack.sh).

.if !defined(_FCFT_MAKEFILE_INC_)
_FCFT_MAKEFILE_INC_=	1

.include <bsd.own.mk>

FCFTDIST=	${NETBSDSRCDIR}/external/mit/fcft/dist

.endif
