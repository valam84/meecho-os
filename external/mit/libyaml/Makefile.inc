# libyaml 0.2.5 (stage 11.7: what yambar reads its configuration with),
# reachover over the import in dist/ (port/gfx/import-stack.sh).

.if !defined(_LIBYAML_MAKEFILE_INC_)
_LIBYAML_MAKEFILE_INC_=	1

.include <bsd.own.mk>

LIBYAMLDIST=	${NETBSDSRCDIR}/external/mit/libyaml/dist

.endif
