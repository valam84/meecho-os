# libffi 3.8.0 (stage 11: libwayland calls listeners through it), reachover
# over the import in dist/ (port/gfx/import-stack.sh).

.if !defined(_LIBFFI_MAKEFILE_INC_)
_LIBFFI_MAKEFILE_INC_=	1

.include <bsd.own.mk>

LIBFFI_VERSION=		3.8.0
LIBFFI_VERSION_NUMBER=	30800
LIBFFIDIST=		${NETBSDSRCDIR}/external/mit/libffi/dist

.endif
