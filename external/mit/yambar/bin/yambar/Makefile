# yambar, the panel of the desktop (stage 11.7), as meson builds it here:
# the Wayland back end alone, its modules, particles and decorations
# compiled in (not loaded as shared objects - the userland is static), the
# modules that have something to read on MEECHO - clock, label, script,
# foreign-toplevel - and docs not built (scdoc).
#
# Its threads - one per module, one for the bar, each waiting in poll(2) -
# are libmthread's, and wait in libgfxcompat's gfx_poll(), which lets the
# others run (minix/lib/libgfxcompat/poll.c): <poll.h> redirects poll().
#
# Beside this file: map.tab.c, map.tab.h and map.yy.c, the map particle's
# grammar, made from dist/particles/map.y and map.l at import by the build
# host's bison 3.8.2 (bison -Wall --defines=map.tab.h --output=map.tab.c)
# and flex 2.6.4 (flex -o map.yy.c), from this directory; version.h by hand.

.include <bsd.init.mk>

PROG=		yambar
MAN=
BINDIR=		/usr/bin

.PATH:		${YAMBARDIST} ${YAMBARDIST}/bar ${YAMBARDIST}/decorations \
		${YAMBARDIST}/particles ${YAMBARDIST}/modules

SRCS=		char32.c config-verify.c config.c log.c main.c module.c \
		particle.c plugin.c tag.c yml.c
SRCS+=		bar.c wayland.c
SRCS+=		background.c border.c overline.c stack.c underline.c
SRCS+=		dynlist.c empty.c list.c map.c progress-bar.c ramp.c string.c \
		map.tab.c map.yy.c
SRCS+=		clock.c label.c script.c foreign-toplevel.c

# The protocols, as yambar names them: client header NAME.h, code NAME.c.
YAMBAR_PROTOCOLS= \
	${WLPROTOCOLSDIST}/stable/xdg-shell/xdg-shell.xml \
	${WLPROTOCOLSDIST}/unstable/xdg-output/xdg-output-unstable-v1.xml \
	${YAMBARDIST}/external/wlr-layer-shell-unstable-v1.xml \
	${YAMBARDIST}/external/wlr-foreign-toplevel-management-unstable-v1.xml

.for _x in ${YAMBAR_PROTOCOLS}
${_x:T:R}.h: ${_x}
	${_MKTARGET_CREATE}
	${TOOL_WAYLAND_SCANNER} client-header ${.ALLSRC} ${.TARGET}
${_x:T:R}.c: ${_x}
	${_MKTARGET_CREATE}
	${TOOL_WAYLAND_SCANNER} private-code ${.ALLSRC} ${.TARGET}
SRCS+=		${_x:T:R}.c
DPSRCS+=	${_x:T:R}.h
CLEANFILES+=	${_x:T:R}.c ${_x:T:R}.h
.endfor

# What meson.build defines here: memfd_create(2) found, a release build,
# Wayland, and a HAVE_PLUGIN_ for every module, particle and decoration
# compiled in - plugin.c's table of them.
CPPFLAGS+=	-D_GNU_SOURCE -D_NETBSD_SOURCE -DNDEBUG -DMEMFD_CREATE \
		-DENABLE_WAYLAND
.for _p in clock label script foreign_toplevel \
	empty list map progress_bar ramp string \
	background border stack underline overline
CPPFLAGS+=	-DHAVE_PLUGIN_${_p}
.endfor
CPPFLAGS+=	-I. -I${.CURDIR} -I${YAMBARDIST} -I${YAMBARDIST}/particles
CPPFLAGS+=	-I${DESTDIR}/usr/include/pixman-1 \
		-I${DESTDIR}/usr/include/freetype2 \
		-I${DESTDIR}/usr/include/libepoll-shim \
		-I${DESTDIR}/usr/include/gfxcompat
# c18, as meson.build has it - through CSTD, which bsd.sys.mk puts last
# (-std in COPTS would come before its gnu99, and lose).
CSTD=		c18
COPTS+=		-fno-asynchronous-unwind-tables
COPTS+=		-fmacro-prefix-map=${YAMBARDIST}/=

LDADD+=	-lyaml -lfcft -lfontconfig -lfreetype -lexpat -lz -lpixman-1 \
	-lwayland-cursor -lwayland-client -lepoll-shim -lffi \
	-lgfxcompat -lmthread -lm
DPADD+=	${DESTDIR}/usr/lib/libyaml.a ${DESTDIR}/usr/lib/libfcft.a \
	${DESTDIR}/usr/lib/libfontconfig.a ${DESTDIR}/usr/lib/libfreetype.a \
	${LIBEXPAT} ${LIBZ} ${DESTDIR}/usr/lib/libpixman-1.a \
	${DESTDIR}/usr/lib/libwayland-cursor.a \
	${DESTDIR}/usr/lib/libwayland-client.a \
	${DESTDIR}/usr/lib/libepoll-shim.a ${DESTDIR}/usr/lib/libffi.a \
	${DESTDIR}/usr/lib/libgfxcompat.a ${LIBMTHREAD} ${LIBM}

.include <bsd.prog.mk>
