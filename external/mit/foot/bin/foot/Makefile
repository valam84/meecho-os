# foot, the terminal of the desktop (stage 11.4), as meson builds it here:
# with IME (text-input-v3: its text field brings the on-screen keyboard
# up, stage 11.9), no grapheme clustering (utf8proc), no utmp, no themes or
# terminfo installed, docs not built (scdoc); TERM is "foot", and the
# capabilities foot answers itself (XTGETTCAP) are built in.
#
# Beside this file, made by foot's python generators and taken from the
# prototype's build of this tag: emoji-variation-sequences.h (from
# unicode/emoji-variation-sequences.txt), foot-terminfo.h (from foot.info;
# the generator writes the build host's uname -s into query-os-name, and
# that is MEECHO here), srgb.c and srgb.h; version.h by hand.

.include <bsd.init.mk>

PROG=		foot
MAN=
BINDIR=		/usr/bin

.PATH:		${FOOTDIST}

# meson's libraries common, misc, vtlib, pgolib, and the program itself.
SRCS=		log.c char32.c debug.c xmalloc.c xsnprintf.c
SRCS+=		hsl.c misc.c uri.c
SRCS+=		base64.c composed.c cursor-shape.c csi.c dcs.c osc.c sixel.c \
		vt.c srgb.c
SRCS+=		grid.c selection.c terminal.c
SRCS+=		async.c box-drawing.c config.c commands.c extract.c fdm.c \
		foot-features.c ime.c input.c key-binding.c main.c notify.c \
		quirks.c reaper.c render.c search.c server.c shm.c slave.c \
		spawn.c tokenize.c unicode-mode.c url-mode.c \
		user-notification.c wayland.c

# The protocols, as foot names them: client header NAME.h, code NAME.c.
FOOT_PROTOCOLS= \
	stable/xdg-shell/xdg-shell.xml \
	unstable/xdg-decoration/xdg-decoration-unstable-v1.xml \
	unstable/xdg-output/xdg-output-unstable-v1.xml \
	unstable/primary-selection/primary-selection-unstable-v1.xml \
	stable/presentation-time/presentation-time.xml \
	unstable/text-input/text-input-unstable-v3.xml \
	staging/xdg-activation/xdg-activation-v1.xml \
	stable/viewporter/viewporter.xml \
	staging/fractional-scale/fractional-scale-v1.xml \
	unstable/tablet/tablet-unstable-v2.xml \
	staging/cursor-shape/cursor-shape-v1.xml \
	staging/single-pixel-buffer/single-pixel-buffer-v1.xml \
	staging/xdg-toplevel-icon/xdg-toplevel-icon-v1.xml \
	staging/xdg-system-bell/xdg-system-bell-v1.xml \
	staging/color-management/color-management-v1.xml \
	staging/xdg-toplevel-tag/xdg-toplevel-tag-v1.xml \
	staging/ext-background-effect/ext-background-effect-v1.xml

.for _x in ${FOOT_PROTOCOLS}
${_x:T:R}.h: ${WLPROTOCOLSDIST}/${_x}
	${_MKTARGET_CREATE}
	${TOOL_WAYLAND_SCANNER} client-header ${.ALLSRC} ${.TARGET}
${_x:T:R}.c: ${WLPROTOCOLSDIST}/${_x}
	${_MKTARGET_CREATE}
	${TOOL_WAYLAND_SCANNER} private-code ${.ALLSRC} ${.TARGET}
SRCS+=		${_x:T:R}.c
DPSRCS+=	${_x:T:R}.h
CLEANFILES+=	${_x:T:R}.c ${_x:T:R}.h
.endfor

# What meson.build defines here: memfd_create(2) and execvpe(3) found,
# pixman 0.46 (RGBA16), wayland-protocols 1.49 (toplevel tag, background
# effect).
CPPFLAGS+=	-D_GNU_SOURCE=200809L -D_NETBSD_SOURCE -DNDEBUG \
		-DFOOT_DEFAULT_TERM=\"foot\" -DMEMFD_CREATE -DEXECVPE \
		-DHAVE_PIXMAN_RGBA_16 -DHAVE_XDG_TOPLEVEL_TAG=1 \
		-DHAVE_EXT_BACKGROUND_EFFECT=1 -DFOOT_IME_ENABLED=1
CPPFLAGS+=	-I. -I${.CURDIR} -I${FOOTDIST}
CPPFLAGS+=	-I${DESTDIR}/usr/include/pixman-1 \
		-I${DESTDIR}/usr/include/freetype2 \
		-I${DESTDIR}/usr/include/libepoll-shim \
		-I${DESTDIR}/usr/include/gfxcompat
COPTS+=		-std=c11 -fstrict-aliasing -fno-asynchronous-unwind-tables
COPTS+=		-fmacro-prefix-map=${FOOTDIST}/=

LDADD+=	-lfcft -lfontconfig -lfreetype -lexpat -lz -lpixman-1 \
	-lwayland-cursor -lwayland-client -lxkbcommon -lepoll-shim -lffi \
	-lgfxcompat -lmthread -lm
DPADD+=	${DESTDIR}/usr/lib/libfcft.a ${DESTDIR}/usr/lib/libfontconfig.a \
	${DESTDIR}/usr/lib/libfreetype.a ${LIBEXPAT} ${LIBZ} \
	${DESTDIR}/usr/lib/libpixman-1.a ${DESTDIR}/usr/lib/libwayland-cursor.a \
	${DESTDIR}/usr/lib/libwayland-client.a ${DESTDIR}/usr/lib/libxkbcommon.a \
	${DESTDIR}/usr/lib/libepoll-shim.a ${DESTDIR}/usr/lib/libffi.a \
	${DESTDIR}/usr/lib/libgfxcompat.a ${LIBMTHREAD} ${LIBM}

.include <bsd.prog.mk>
