#!/bin/bash
#
# The Wayland stack of stage 11, built for MEECHO by its own build systems
# (meson, autotools) with the port's cross toolchain - the prototype that
# finds what each package needs from the system and what it needs patched,
# before the package goes into external/ with reachover Makefiles.
#
#   build-stack.sh sysroot          copy DESTDIR's headers and libraries
#                                   into ~/gfx-sysroot (again: updates)
#   build-stack.sh PKG...           build and install PKG into the sysroot
#   build-stack.sh all              every package, in order
#
# Sources are in ~/gfx-src (see port/gfx/README: which tag of what), build
# directories in ~/gfx-build/PKG, logs in ~/gfx-build/PKG.log.
set -u

PORT=$(cd "$(dirname "$0")/.." && pwd)
SRC=${GFX_SRC:-$HOME/gfx-src}
BUILD=${GFX_BUILD:-$HOME/gfx-build}
SYSROOT=${GFX_SYSROOT:-$HOME/gfx-sysroot}
DEST=${DESTDIR:-$HOME/dest-evbarm64}
CROSS=$PORT/gfx/cross-meecho.ini
TOOLS=$HOME/xtools-aarch64/bin/aarch64-elf64-minix-
HOST=aarch64-elf64-minix
JOBS=${JOBS:-24}
# Host tools newer than the distribution's: wayland-scanner 1.26 (the XML
# of wayland-protocols 1.49 fails the DTD of 1.24's), built by
# "build-stack.sh native".
NATIVE=${GFX_NATIVE:-$HOME/gfx-native}
# And meson itself, newer than the distribution's (fontconfig 2.18 wants
# 1.11): ~/gfx-native/bin/meson is a venv's (pip install meson).
PATH=$NATIVE/bin:$PATH
export PATH

ORDER="libffi wayland wayland-protocols pixman drm libxkbcommon
	xkeyboard-config wlroots expat freetype fontconfig tllist fcft foot"

mkdir -p "$BUILD"

say() { printf '%s\n' "$*"; }

sysroot() {
	mkdir -p "$SYSROOT/usr"
	rsync -a "$DEST/usr/include" "$DEST/usr/lib" "$SYSROOT/usr/" || return 1
	# The system has no pthread.h; libmthread has the calls, behind
	# _MTHREADIFY_PTHREADS.  Here only, for the stack: a system-wide
	# pthread.h would change how everything else configures (OpenSSL
	# is built thread-less on purpose).
	cat > "$SYSROOT/usr/include/pthread.h" <<'EOF'
/* MEECHO, gfx sysroot only: POSIX thread names over libmthread. */
#ifndef _GFX_PTHREAD_H
#define _GFX_PTHREAD_H
#define _MTHREADIFY_PTHREADS
#include <minix/mthread.h>
#endif
EOF
	# libpthread.a = libmthread's objects and the compat ones: sem_*
	# and C11 thrd_create/thrd_join (compat/compat.c), with <threads.h>
	# over the same names (compat/threads.h).
	cp "$PORT/gfx/compat/threads.h" "$SYSROOT/usr/include/threads.h"
	rm -f "$SYSROOT/usr/lib/libpthread.a"
	mkdir -p "$BUILD/compat" &&
	"$PORT/gfx/cc.sh" --sysroot="$SYSROOT" -O2 -c \
	    "$PORT/gfx/compat/compat.c" -o "$BUILD/compat/compat.o" &&
	cp "$SYSROOT/usr/lib/libmthread.a" "$SYSROOT/usr/lib/libpthread.a" &&
	${TOOLS}ar rs "$SYSROOT/usr/lib/libpthread.a" "$BUILD/compat/compat.o" ||
	    return 1
	# <uchar.h>, which the libc predates, and its conversions - in
	# libuchar.a, which cc.sh adds to every link (compat/uchar.c).
	cp "$PORT/gfx/compat/uchar.h" "$SYSROOT/usr/include/uchar.h" &&
	"$PORT/gfx/cc.sh" --sysroot="$SYSROOT" -O2 -c \
	    "$PORT/gfx/compat/uchar.c" -o "$BUILD/compat/uchar.o" &&
	rm -f "$SYSROOT/usr/lib/libuchar.a" &&
	${TOOLS}ar rcs "$SYSROOT/usr/lib/libuchar.a" "$BUILD/compat/uchar.o" ||
	    return 1
	# clock_gettime and friends are in libc; wlroots asks for -lrt.
	[ -f "$SYSROOT/usr/lib/librt.a" ] || ${TOOLS}ar rc "$SYSROOT/usr/lib/librt.a"
	# The key and button codes wlroots and foot take from Linux's
	# <linux/input-event-codes.h>, which is GPL: FreeBSD's own, BSD-2,
	# same numbers (sys/dev/evdev; ~/gfx-src/freebsd-evdev).
	mkdir -p "$SYSROOT/usr/include/linux"
	cp "$SRC/freebsd-evdev/input-event-codes.h" \
	    "$SYSROOT/usr/include/linux/input-event-codes.h"
	say "sysroot: $SYSROOT"
}

# meson PKG SRCDIR [options...]
meson_pkg() {
	local name=$1 src=$2 b=$BUILD/$1
	shift 2
	rm -rf "$b"
	PATH=$NATIVE/bin:$PATH meson setup "$b" "$src" --cross-file "$CROSS" \
	    --prefix=/usr --libdir=lib --buildtype=release \
	    -Dbuild.pkg_config_path="$NATIVE/lib/pkgconfig" "$@" &&
	ninja -C "$b" -j "$JOBS" ${TARGETS:-} &&
	DESTDIR="$SYSROOT" meson install -C "$b" --no-rebuild
}

build_libffi() {
	local b=$BUILD/libffi
	( cd "$SRC/libffi" && { [ -x configure ] || ./autogen.sh; } ) || return 1
	rm -rf "$b" && mkdir -p "$b" && cd "$b" &&
	"$SRC/libffi/configure" --host=$HOST --prefix=/usr \
	    --disable-shared --enable-static --disable-docs \
	    --disable-multi-os-directory --disable-exec-static-tramp \
	    CC="${TOOLS}gcc --sysroot=$SYSROOT" AR=${TOOLS}ar \
	    RANLIB=${TOOLS}ranlib CFLAGS=-O2 &&
	make -j "$JOBS" && make install DESTDIR="$SYSROOT"
}

build_native() {
	local b=$BUILD/wayland-native
	rm -rf "$b"
	meson setup "$b" "$SRC/wayland" --prefix="$NATIVE" --libdir=lib \
	    -Dlibraries=false -Dscanner=true -Dtests=false \
	    -Ddocumentation=false -Ddtd_validation=false &&
	ninja -C "$b" && meson install -C "$b"
}

build_wayland() {
	meson_pkg wayland "$SRC/wayland" -Dlibraries=true -Dscanner=false \
	    -Dtests=false -Ddocumentation=false -Ddtd_validation=false
}

build_wayland-protocols() {
	meson_pkg wayland-protocols "$SRC/wayland-protocols" -Dtests=false
}

build_pixman() {
	meson_pkg pixman "$SRC/pixman" -Dgtk=disabled -Dlibpng=disabled \
	    -Dtests=disabled -Ddemos=disabled -Dopenmp=disabled \
	    -Dtimers=false -Da64-neon=disabled
}

build_drm() {
	meson_pkg drm "$SRC/drm" -Dintel=disabled -Dradeon=disabled \
	    -Damdgpu=disabled -Dnouveau=disabled -Dvmwgfx=disabled \
	    -Domap=disabled -Dexynos=disabled -Dfreedreno=disabled \
	    -Dtegra=disabled -Dvc4=disabled -Detnaviv=disabled \
	    -Dcairo-tests=disabled -Dman-pages=disabled -Dvalgrind=disabled \
	    -Dtests=false -Dinstall-test-programs=false
}

build_libxkbcommon() {
	# Its tests and benchmarks are always built and cannot be turned
	# off; only the library is wanted (and the benchmarks need
	# CLOCK_PROCESS_CPUTIME_ID, which MINIX has not).
	TARGETS=libxkbcommon.a \
	meson_pkg libxkbcommon "$SRC/libxkbcommon" -Denable-x11=false \
	    -Denable-wayland=false -Denable-docs=false -Denable-tools=false \
	    -Denable-xkbregistry=false -Denable-bash-completion=false \
	    -Dxkb-config-root=/usr/share/X11/xkb \
	    -Dx-locale-root=/usr/share/X11/locale
}

build_xkeyboard-config() {
	meson_pkg xkeyboard-config "$SRC/xkeyboard-config" \
	    -Dxorg-rules-symlinks=false -Dcompat-rules=true
}

build_wlroots() {
	meson_pkg wlroots "$SRC/wlroots" -Dxwayland=disabled -Dbackends= \
	    -Drenderers= -Dallocators= -Dsession=disabled -Dexamples=false \
	    -Dcolor-management=disabled -Dlibliftoff=disabled \
	    -Dxcb-errors=disabled
}

# expat for fontconfig: the tree's own (external/mit/expat), by its
# configure, into the sysroot - DESTDIR does not build it.
build_expat() {
	# As the tree's reachover builds it (external/mit/expat/lib/libexpat):
	# three files and NetBSD's expat_config.h.  Its configure is too old
	# to know aarch64.
	local b=$BUILD/expat x=$PORT/../external/mit/expat f
	rm -rf "$b" && mkdir -p "$b" || return 1
	for f in xmlparse xmlrole xmltok; do
		"$PORT/gfx/cc.sh" --sysroot="$SYSROOT" -O2 -DHAVE_EXPAT_CONFIG_H \
		    -I"$x/lib/libexpat" -I"$x/dist/lib" -c "$x/dist/lib/$f.c" \
		    -o "$b/$f.o" || return 1
	done
	${TOOLS}ar rcs "$SYSROOT/usr/lib/libexpat.a" "$b"/*.o &&
	cp "$x/dist/lib/expat.h" "$x/dist/lib/expat_external.h" \
	    "$SYSROOT/usr/include/" &&
	printf '%s\n' 'prefix=/usr' 'libdir=${prefix}/lib' \
	    'includedir=${prefix}/include' '' 'Name: expat' \
	    'Description: expat XML parser (the tree'"'"'s)' 'Version: 2.1.0' \
	    'Libs: -L${libdir} -lexpat' 'Cflags: -I${includedir}' \
	    > "$SYSROOT/usr/lib/pkgconfig/expat.pc"
}

build_freetype() {
	meson_pkg freetype "$SRC/freetype" -Dbrotli=disabled -Dbzip2=disabled \
	    -Dharfbuzz=disabled -Dpng=disabled -Dzlib=internal -Dtests=disabled
}

build_fontconfig() {
	meson_pkg fontconfig "$SRC/fontconfig" -Ddoc=disabled -Dnls=disabled \
	    -Dtests=disabled -Dtools=enabled -Dcache-build=disabled \
	    -Dxml-backend=expat -Diconv=disabled \
	    -Ddefault-fonts-dirs=/usr/share/fonts
}

build_tllist() {
	meson_pkg tllist "$SRC/tllist"
}

build_fcft() {
	meson_pkg fcft "$SRC/fcft" -Dsvg-backend=none -Dtest-text-shaping=false \
	    -Dexamples=false
}

build_foot() {
	# -Dwerror=false: utils/xtgettcap passes a char to <ctype.h>, whose
	# NetBSD macros index a table with it (-Wchar-subscripts).
	meson_pkg foot "$SRC/foot" -Dwerror=false -Ddocs=disabled -Dthemes=false -Dime=false \
	    -Dgrapheme-clustering=disabled -Dtests=false -Dterminfo=disabled \
	    -Dutmp-backend=none &&
	mkdir -p "$BUILD/bin" && cp "$SYSROOT/usr/bin/foot" "$BUILD/bin/foot"
}

pc() {
	PKG_CONFIG_SYSROOT_DIR=$SYSROOT \
	PKG_CONFIG_LIBDIR=$SYSROOT/usr/lib/pkgconfig:$SYSROOT/usr/share/pkgconfig \
	    pkg-config "$@"
}

# xdg-shell, generated: KIND (server-header, client-header, private-code)
xdg_shell() {
	"$NATIVE/bin/wayland-scanner" "$1" \
	    "$(pc --variable=pkgdatadir wayland-protocols)/stable/xdg-shell/xdg-shell.xml" "$2"
}

# The programs built here go to ~/gfx-build/bin, for the disk image.
build_tinywl() {
	local b=$BUILD/tinywl pkgs="wlroots-0.20 wayland-server xkbcommon"
	mkdir -p "$b" "$BUILD/bin" &&
	xdg_shell server-header "$b/xdg-shell-protocol.h" &&
	"$PORT/gfx/cc.sh" --sysroot="$SYSROOT" -O2 -g -DWLR_USE_UNSTABLE \
	    -D_NETBSD_SOURCE -I"$b" $(pc --cflags $pkgs) \
	    "$SRC/wlroots/tinywl/tinywl.c" -o "$BUILD/bin/tinywl" -static \
	    $(pc --static --libs $pkgs)
}

build_meechowm() {
	local b=$BUILD/meechowm m=$PORT/gfx/meechowm
	local pkgs="wlroots-0.20 wayland-server xkbcommon pixman-1 libdrm"
	mkdir -p "$b" "$BUILD/bin" &&
	xdg_shell server-header "$b/xdg-shell-protocol.h" &&
	"$PORT/gfx/cc.sh" --sysroot="$SYSROOT" -O2 -g -Wall -DWLR_USE_UNSTABLE \
	    -D_NETBSD_SOURCE -I"$b" -I"$m" \
	    -I"$PORT/../minix/drivers/video/fb/arch/aarch64" \
	    $(pc --cflags $pkgs) "$m/meechowm.c" "$m/fbout.c" "$m/input.c" \
	    "$m/hid2evdev.c" -o "$BUILD/bin/meechowm" -static \
	    $(pc --static --libs $pkgs)
}

build_wlhello() {
	local b=$BUILD/wlhello pkgs="wayland-client"
	mkdir -p "$b" "$BUILD/bin" &&
	xdg_shell client-header "$b/xdg-shell-client-protocol.h" &&
	xdg_shell private-code "$b/xdg-shell-protocol.c" &&
	"$PORT/gfx/cc.sh" --sysroot="$SYSROOT" -O2 -g -D_NETBSD_SOURCE -I"$b" \
	    $(pc --cflags $pkgs) "$PORT/gfx/test/wlhello.c" \
	    "$b/xdg-shell-protocol.c" -o "$BUILD/bin/wlhello" -static \
	    $(pc --static --libs $pkgs)
}

# What foot asks of fcft, alone (test/fcftprobe.c).
build_fcftprobe() {
	mkdir -p "$BUILD/bin" &&
	"$PORT/gfx/cc.sh" --sysroot="$SYSROOT" -O2 -g -D_NETBSD_SOURCE \
	    $(pc --cflags fcft) "$PORT/gfx/test/fcftprobe.c" \
	    -o "$BUILD/bin/fcftprobe" -static $(pc --static --libs fcft)
}

one() {
	local p=$1 log=$BUILD/$1.log
	if [ "$p" = sysroot ]; then sysroot; return; fi
	if ! declare -F "build_$p" >/dev/null; then
		say "no such package: $p"
		return 2
	fi
	if ( "build_$p" ) > "$log" 2>&1; then
		say "ok   $p"
	else
		say "FAIL $p  ($log)"
		grep -m5 -E "error|Error|ERROR|undefined reference" "$log" |
		    cut -c1-200
		return 1
	fi
}

[ $# -gt 0 ] || { sed -n '3,16p' "$0"; exit 2; }
[ "$1" = all ] && set -- sysroot $ORDER
for p in "$@"; do
	one "$p" || exit 1
done
