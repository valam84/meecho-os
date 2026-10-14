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
# the tag, not them), ~/gfxport/nb-xsrc (NB_XSRC: a sparse clone of NetBSD's
# xsrc - git clone --filter=blob:none --no-checkout
# https://github.com/NetBSD/xsrc, then sparse-checkout set
# /external/mit/pixman /external/mit/libdrm /external/mit/xkeyboard-config).
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

# fuzzel (stage 11.7: the launcher and the command palette), without icons:
# no libpng, no SVG back end, no cairo - so the bundled nanosvg (nanosvg.c,
# nanosvgrast.c, 3rd-party/) is left out.  Its own copy of the layer-shell
# protocol comes with it.
imp_fuzzel() {
	from_git external/mit/fuzzel/dist fuzzel LICENSE README.md \
	    CHANGELOG.md fuzzel.ini srgb.py \
	    external/wlr-layer-shell-unstable-v1.xml \
	    $(git -C "$SRC/fuzzel" ls-tree --name-only HEAD |
	    grep -E '\.[ch]$' | grep -v '^nanosvg')
}

# libyaml 0.2.5 (MIT), what yambar reads its configuration with: the
# library's sources and its one public header, not its tests or tools.
imp_libyaml() {
	from_git external/mit/libyaml/dist libyaml License ReadMe.md \
	    Changes include/yaml.h src &&
	rm -f "$TREE"/external/mit/libyaml/dist/src/Makefile.am
}

# yambar (stage 11.7: the panel), the Wayland back end only (no xcb.c),
# with the modules that have something to read on MEECHO - clock, label,
# script, foreign-toplevel; the rest read Linux's /proc and /sys, netlink,
# udev, ALSA, PulseAudio, PipeWire, MPD or a compositor's IPC.  Every
# particle and decoration; map's grammar is generated at build time.
imp_yambar() {
	local m
	from_git external/mit/yambar/dist yambar LICENSE README.md \
	    CHANGELOG.md external/wlr-layer-shell-unstable-v1.xml \
	    external/wlr-foreign-toplevel-management-unstable-v1.xml \
	    bar/bar.c bar/bar.h bar/backend.h bar/private.h bar/wayland.c \
	    bar/wayland.h decorations particles \
	    modules/clock.c modules/label.c modules/script.c \
	    modules/foreign-toplevel.c \
	    $(git -C "$SRC/yambar" ls-tree --name-only HEAD |
	    grep -E '\.[ch]$' | grep -v '^xcb\.') &&
	for m in decorations particles; do
		rm -f "$TREE"/external/mit/yambar/dist/$m/meson.build
	done
}

# One header of FreeBSD's (sys/dev/evdev, BSD-2): the key and button codes
# Linux keeps in its GPL <linux/input-event-codes.h>, same numbers.
imp_evdev() {
	local dest=$TREE/external/bsd/evdev/dist
	rm -rf "$dest" && mkdir -p "$dest" &&
	cp "$SRC/freebsd-evdev/input-event-codes.h" "$dest/"
}

# The desktop's other programs (stage 11.7, PLAN.md): the wallpaper, the
# screenshot, the file manager, and the typeface.
#
# libpng 1.6.59 (the libpng licence, permissive): the library, NEON code
# for arm, and the configuration header upstream ships ready-made - no
# awk-generated one, no tests, no tools.
imp_libpng() {
	from_git external/mit/libpng/dist libpng LICENSE README AUTHORS \
	    png.h pngconf.h pngdebug.h pnginfo.h pngpriv.h pngstruct.h \
	    png.c pngerror.c pngget.c pngmem.c pngpread.c pngread.c pngrio.c \
	    pngrtran.c pngrutil.c pngset.c pngtrans.c pngwio.c pngwrite.c \
	    pngwtran.c pngwutil.c arm/arm_init.c arm/filter_neon_intrinsics.c \
	    arm/palette_neon_intrinsics.c scripts/pnglibconf.h.prebuilt
}

# wbg 1.3.0 (MIT; nanosvg, bundled, zlib licence): PNG and SVG only - the
# JPEG, WebP and JPEG XL readers stay out with their libraries.
imp_wbg() {
	from_git external/mit/wbg/dist wbg LICENSE README.md CHANGELOG.md \
	    external/wlr-layer-shell-unstable-v1.xml \
	    3rd-party/nanosvg/LICENSE.txt 3rd-party/nanosvg/src nanosvg \
	    log.c log.h main.c nanosvg.c nanosvgrast.c png.c png-wbg.h shm.c \
	    shm.h stride.h svg.c svg.h wbg-features.h
}

# grim 1.5.0 (MIT): the program and its own copy of wlr-screencopy; the
# rest of its protocols are wayland-protocols'.  Its manual is scdoc's.
imp_grim() {
	from_git external/mit/grim/dist grim LICENSE README.md \
	    box.c buffer.c main.c output-layout.c render.c write_jpg.c \
	    write_png.c write_ppm.c include protocol/wlr-screencopy-unstable-v1.xml
}

# nnn 5.3 (BSD-2): the one program, its header and manual - without the
# icon tables, the plugins (bash) and upstream's patches.
imp_nnn() {
	from_git external/bsd/nnn/dist nnn LICENSE README.md CHANGELOG nnn.1 \
	    src/nnn.c src/nnn.h src/dbg.h src/qsort.h
}

# The Go fonts (BSD-3), from golang.org/x/image's font/gofont/ttfs, the
# README with the licence.  GOIMAGE: a clone with that directory checked
# out (git clone --filter=blob:none --sparse, sparse-checkout set
# font/gofont/ttfs).
imp_gofont() {
	from_git external/bsd/gofont/dist ${GOIMAGE:-goimage} \
	    font/gofont/ttfs &&
	mv "$TREE"/external/bsd/gofont/dist/font/gofont/ttfs/* \
	    "$TREE"/external/bsd/gofont/dist/ &&
	rm -r "$TREE"/external/bsd/gofont/dist/font
}

# cairo 1.18.6, under the MPL 1.1 - cairo is MPL-1.1 or LGPL-2.1, and
# MEECHO takes it under the first, so COPYING-LGPL-2.1 stays out.  The
# library's sources for what is built here (external/mit/cairo/Makefile):
# the image surface on pixman, PNG, the SVG surface that comes with it,
# FreeType and fontconfig, recording - exactly the files meson's build of
# that reads.  Not X11, Quartz, Win32, PostScript, PDF or the script
# surface, nor the tests, tools and script interpreter (util/).
imp_cairo() {
	local d=$TREE/external/mit/cairo/dist/src
	from_git external/mit/cairo/dist cairo COPYING COPYING-MPL-1.1 \
	    README.md AUTHORS src &&
	rm -rf "$d"/win32 "$d"/meson.build "$d"/README "$d"/check-* \
	    "$d"/test-* "$d"/*xlib* "$d"/*xcb* "$d"/*quartz* "$d"/*script* \
	    "$d"/*tee* "$d"/cairo-ps* "$d"/cairo-pdf.h "$d"/cairo-pdf-surface* \
	    "$d"/cairo-pdf-interchange.c "$d"/cairo-dwrite.h "$d"/cairo-win32.h \
	    "$d"/cairo-image-mask-compositor.c "$d"/cairo-error-inline.h
}

# slurp 1.5.0 (MIT): the area picker of meechowm-shot -r - the program,
# its headers and its own copy of wlr-layer-shell; the rest of its
# protocols are wayland-protocols'.  Its manual is scdoc's.
imp_slurp() {
	from_git external/mit/slurp/dist slurp LICENSE README.md main.c \
	    pool-buffer.c render.c include \
	    protocol/wlr-layer-shell-unstable-v1.xml
}

ALL="libffi wayland wayland_protocols pixman libdrm libxkbcommon
	xkeyboard_config wlroots freetype fontconfig tllist fcft foot fuzzel
	libyaml yambar evdev libpng wbg grim nnn gofont cairo slurp"
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
