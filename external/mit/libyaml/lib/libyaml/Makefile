# libyaml, as its autotools build makes it, without config.h: the version
# is all that config.h would say.

.include <bsd.init.mk>

LIB=		yaml

.PATH:		${LIBYAMLDIST}/src ${LIBYAMLDIST}/include
SRCS=		api.c dumper.c emitter.c loader.c parser.c reader.c scanner.c \
		writer.c

CPPFLAGS+=	-DYAML_VERSION_MAJOR=0 -DYAML_VERSION_MINOR=2 \
		-DYAML_VERSION_PATCH=5 -DYAML_VERSION_STRING=\"0.2.5\"
CPPFLAGS+=	-I${LIBYAMLDIST}/include -I${LIBYAMLDIST}/src

INCS=		yaml.h
INCSDIR=	/usr/include

.include <bsd.lib.mk>
