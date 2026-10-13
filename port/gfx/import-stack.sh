#!/bin/bash
#
# Import the Wayland stack of stage 11 into external/: each package's dist
# directory, made from a pristine upstream tag (or NetBSD-current's xsrc,
# where NetBSD carries the package), and trimmed to what the reachover
# build uses plus the licence - never the tests, CI, documentation sources
# or upstream build systems, and never a GPL file (libffi's and FreeType's
# autotools helpers, libxkbcommon's test/evdev-scancodes.h, libdrm's
# etnaviv header).  wlroots' protocol/server-decoration.xml is LGPL (KDE's
# protocol) and is left out with the code built from it.
#
#   import-stack.sh [PKG...]        default: every package
#
# Sources: ~/gfx-src (shallow clones of the tags in port/gfx/README; the
# working trees may carry the prototype's patches - "git archive HEAD" takes
# the tag, not them), ~/gfxport/nb-xsrc (a sparse clone of NetBSD's xsrc).
# The MEECHO changes are separate commits on top of the import, so that the
# next import is a diff against this one.
set -u
PORT=$(cd "$(dirname "$0")/.." && pwd)
TREE=${SRCDIR:-$(cd "$PORT/.." && pwd)}
SRC=${GFX_SRC:-$HOME/gfx-src}
NBX=${NB_XSRC:-$HOME/gfxport/nb-xsrc}/external/mit

# Upstream's .gitignore files would be ignored by the tree's anyway (it
# lists .gitignore), so they go.
tidy() {
	find "$1" -name .gitignore -delete
}

# from_git PKG-DIR SRCREPO PATH...: the tag's files under PATH, as they are.
from_git() {
	local dest=$TREE/$1 repo=$SRC/$2
	shift 2
	rm -rf "$dest" && mkdir -p "$dest" &&
	git -C "$repo" archive --format=tar HEAD -- "$@" | tar -x -C "$dest" &&
	tidy "$dest"
}

# from_nbx PKG-DIR NBPKG PATH...: NetBSD's dist, NetBSD's local changes in.
from_nbx() {
	local dest=$TREE/$1 src=$NBX/$2/dist
	shift 2
	rm -rf "$dest" && mkdir -p "$dest" &&
	( cd "$src" && tar -cf - "$@" ) | tar -x -C "$dest" &&
	tidy "$dest"
}

imp_libffi() {
	from_git external/mit/libffi/dist libffi LICENSE README.md \
	    include/ffi.h.in include/ffi_cfi.h include/ffi_common.h \
	    include/tramp.h src/closures.c src/debug.c src/dlmalloc.c \
	    src/java_raw_api.c src/prep_cif.c src/raw_api.c src/tramp.c \
	    src/types.c src/aarch64
}

imp_wayland() {
	from_git external/mit/wayland/dist wayland COPYING README.md \
	    src cursor protocol/wayland.xml protocol/wayland.dtd &&
	rm -f "$TREE"/external/mit/wayland/dist/cursor/cursor.pcf
}

imp_wayland_protocols() {
	from_git external/mit/wayland-protocols/dist wayland-protocols \
	    COPYING README.md stable staging unstable experimental
}

imp_pixman() {
	from_nbx external/mit/pixman/dist pixman COPYING README NEWS AUTHORS \
	    pixman
}

imp_libdrm() {
	from_nbx external/mit/libdrm/dist libdrm README.rst core-symbols.txt \
	    gen_table_fourcc.py include/drm libdrm_lists.h libdrm_macros.h \
	    libsync.h util_double_list.h util_math.h xf86atomic.h \
	    xf86drm.c xf86drm.h xf86drmHash.c xf86drmHash.h xf86drmMode.c \
	    xf86drmMode.h xf86drmRandom.c xf86drmRandom.h xf86drmSL.c
}

imp_libxkbcommon() {
	from_git external/mit/libxkbcommon/dist libxkbcommon LICENSE \
	    README.md NEWS.md include src
}

imp_xkeyboard_config() {
	from_nbx external/mit/xkeyboard-config/dist xkeyboard-config \
	    COPYING README NEWS AUTHORS compat geometry keycodes rules \
	    symbols types
}

imp_wlroots() {
	from_git external/mit/wlroots/dist wlroots LICENSE README.md \
	    include protocol backend render types util xcursor &&
	rm -f "$TREE"/external/mit/wlroots/dist/protocol/server-decoration.xml
}

imp_freetype() {
	# src/gzip/patches: FreeType's diffs against the zlib it bundles,
	# which is not built here (the system's libz is).
	from_git external/mit/freetype/dist freetype LICENSE.TXT README \
	    docs/FTL.TXT docs/CHANGES include src builds/unix/ftsystem.c &&
	rm -rf "$TREE"/external/mit/freetype/dist/src/gzip/patches
}

imp_fontconfig() {
	from_git external/mit/fontconfig/dist fontconfig COPYING README.md \
	    NEWS conf.d fontconfig src fc-cache fc-list fc-match fc-case \
	    fc-lang fc-const fc-genericfamily fonts.conf.in fonts.dtd \
	    config-fixups.h
}

imp_tllist() {
	from_git external/mit/tllist/dist tllist LICENSE README.md tllist.h
}

imp_fcft() {
	from_git external/mit/fcft/dist fcft LICENSE README.md CHANGELOG.md \
	    fcft.c fcft log.c log.h generate-emoji-data.py \
	    generate-unicode-precompose.sh generate-version.sh \
	    unicode/license.txt
}

imp_foot() {
	local d=$TREE/external/mit/foot/dist
	from_git external/mit/foot/dist foot LICENSE README.md CHANGELOG.md \
	    foot.ini foot.info generate-version.sh scripts unicode utils \
	    $(git -C "$SRC/foot" ls-tree --name-only HEAD | grep -E '\.[ch]$')
}

# One header of FreeBSD's (sys/dev/evdev, BSD-2): the key and button codes
# Linux keeps in its GPL <linux/input-event-codes.h>, same numbers.
imp_evdev() {
	local dest=$TREE/external/bsd/evdev/dist
	rm -rf "$dest" && mkdir -p "$dest" &&
	cp "$SRC/freebsd-evdev/input-event-codes.h" "$dest/"
}

ALL="libffi wayland wayland_protocols pixman libdrm libxkbcommon
	xkeyboard_config wlroots freetype fontconfig tllist fcft foot evdev"
[ $# -gt 0 ] || set -- $ALL
for p in "$@"; do
	p=${p//-/_}
	if "imp_$p"; then
		echo "imported $p"
	else
		echo "FAILED $p"
		exit 1
	fi
done
