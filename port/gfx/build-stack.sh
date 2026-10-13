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

ORDER="libffi wayland wayland-protocols pixman drm libxkbcommon
	xkeyboard-config wlroots"

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
	ln -sf libmthread.a "$SYSROOT/usr/lib/libpthread.a"
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
