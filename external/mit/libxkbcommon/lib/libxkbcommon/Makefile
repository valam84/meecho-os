# The library only - no X11 or Wayland helper programs, no xkbregistry.
#
# Beside this file, besides config.h:
#   xkbcomp-parser.c, parser.h  the keymap parser, made from
#	dist/src/xkbcomp/parser.y by bison 3.8.2 (--no-lines, -p _xkbcommon_,
#	as meson runs it).  The tree's yacc is a 2015 byacc and knows neither
#	%code requires nor parse.error detailed.  The skeleton's GPL carries
#	Bison's exception: the generated parser is used under the library's
#	own licence.  Regenerate after an import of a new parser.y.
#   compose-state.c, xkbcomp-keymap.c  one line each: src/ and its
#	subdirectories have a state.c and a keymap.c each, and objects are
#	named by the file's base name here.

.include <bsd.init.mk>

LIB=		xkbcommon

.PATH:		${XKBCOMMONDIST}/src ${XKBCOMMONDIST}/src/compose \
		${XKBCOMMONDIST}/src/xkbcomp

SRCS=		parser.c paths.c table.c compose-state.c

SRCS+=		action.c ast-build.c compat.c expr.c include.c keycodes.c \
		keymap-dump.c keywords.c rules.c scanner.c symbols.c types.c \
		vmod.c xkbcomp.c xkbcomp-keymap.c xkbcomp-parser.c

SRCS+=		atom.c context.c context-priv.c keysym.c \
		keysym-case-mappings.c keysym-utf.c keymap.c keymap-compare.c \
		keymap-priv.c rmlvo.c scanner-utils.c state.c text.c utf8.c \
		utf8-decoding.c utils.c utils-paths.c

CPPFLAGS+=	-D_NETBSD_SOURCE -I${.CURDIR} -I${XKBCOMMONDIST} \
		-I${XKBCOMMONDIST}/include -I${XKBCOMMONDIST}/src

# What libxkbcommon's own build asks of the compiler (meson.build).
COPTS+=		-std=c11 -fno-strict-aliasing

.PATH:		${XKBCOMMONDIST}/include/xkbcommon
INCS=		xkbcommon.h xkbcommon-compat.h xkbcommon-compose.h \
		xkbcommon-keysyms.h xkbcommon-names.h
INCSDIR=	/usr/include/xkbcommon

.include <bsd.lib.mk>
