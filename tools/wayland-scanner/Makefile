# nbwayland-scanner: the protocol compiler every reachover of the Wayland
# stack runs (external/mit/wayland, wlroots, foot, the MEECHO programs).
#
# It reads XML with expat.  The host's may be absent or another version, so
# the tree's own (external/mit/expat) is compiled into the tool, with the
# same expat_config.h as the target's libexpat.
#
# Since 2.2 expat salts its hashes; 2.8 keeps each entropy source in a
# random_*.c of its own.  The config header gives the host all of them
# (only MINIX is told it lacks getrandom/getentropy), and xmlparse.c
# takes arc4random_buf(3) first: glibc 2.36 or later, any BSD, macOS.

HOSTPROGNAME=	${_TOOL_PREFIX}wayland-scanner
HOST_SRCDIR=	external/mit/wayland/bin/wayland-scanner

HOST_SRCS=	xmlparse.c xmlrole.c xmltok.c \
		random_arc4random.c random_arc4random_buf.c random_dev_urandom.c \
		random_getentropy.c random_getrandom.c

.include "${.CURDIR}/../Makefile.host"

.PATH:		${.CURDIR}/../../external/mit/expat/dist/lib
HOST_CPPFLAGS+=	-DHAVE_EXPAT_CONFIG_H \
		-I${.CURDIR}/../../external/mit/expat/lib/libexpat \
		-I${.CURDIR}/../../external/mit/expat/dist/lib
