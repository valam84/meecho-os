# nbwayland-scanner: the protocol compiler every reachover of the Wayland
# stack runs (external/mit/wayland, wlroots, foot, the MEECHO programs).
#
# It reads XML with expat.  The host's may be absent or another version, so
# the tree's own (external/mit/expat) is compiled into the tool, with the
# same expat_config.h as the target's libexpat.

HOSTPROGNAME=	${_TOOL_PREFIX}wayland-scanner
HOST_SRCDIR=	external/mit/wayland/bin/wayland-scanner

HOST_SRCS=	xmlparse.c xmlrole.c xmltok.c

.include "${.CURDIR}/../Makefile.host"

.PATH:		${.CURDIR}/../../external/mit/expat/dist/lib
HOST_CPPFLAGS+=	-DHAVE_EXPAT_CONFIG_H \
		-I${.CURDIR}/../../external/mit/expat/lib/libexpat \
		-I${.CURDIR}/../../external/mit/expat/dist/lib
