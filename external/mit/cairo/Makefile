# cairo 1.18.6 (stage 11.7): 2D drawing for the desktop's programs that
# draw with it - slurp, the area picker of meechowm-shot -r, first.
# Reachover over the import in dist/ (port/gfx/import-stack.sh), taken
# under the MPL 1.1 (dist/COPYING-MPL-1.1).
#
# What is built is what meson's build of cairo for aarch64-elf64-minix
# builds with -Dpng -Dfontconfig -Dfreetype enabled and zlib, X11, tee,
# glib off (port/gfx/cairo-oracle.sh): the image surface, drawn by pixman;
# PNG through libpng, and the SVG surface that comes with it; FreeType
# and fontconfig fonts; recording.  No GL, no X11, no PostScript, PDF or
# script surface, no script interpreter.  config.h and cairo-features.h
# beside this file are what that build's checks wrote.  Static, as the
# userland is: a program links -lcairo -lpng -lz -lfontconfig -lexpat
# -lfreetype -lpixman-1 -lgfxcompat -lmthread -lm.

NOLINT=	1	# defined

.include <bsd.own.mk>

CAIRODIST=	${.CURDIR}/dist

.PATH:	${CAIRODIST}/src

LIB=	cairo
SRCS=	cairo-analysis-surface.c cairo-arc.c cairo-array.c \
	cairo-atomic.c cairo-base64-stream.c cairo-base85-stream.c \
	cairo-bentley-ottmann-rectangular.c \
	cairo-bentley-ottmann-rectilinear.c cairo-bentley-ottmann.c \
	cairo-botor-scan-converter.c cairo-boxes-intersect.c cairo-boxes.c \
	cairo-cache.c cairo-cff-subset.c cairo-clip-boxes.c \
	cairo-clip-polygon.c cairo-clip-region.c cairo-clip-surface.c \
	cairo-clip-tor-scan-converter.c cairo-clip.c cairo-color.c \
	cairo-composite-rectangles.c \
	cairo-compositor.c cairo-contour.c cairo-damage.c cairo-debug.c \
	cairo-default-context.c cairo-deflate-stream.c cairo-device.c \
	cairo-error.c cairo-fallback-compositor.c cairo-fixed.c \
	cairo-font-face-twin-data.c cairo-font-face-twin.c cairo-font-face.c \
	cairo-font-options.c cairo-freed-pool.c cairo-freelist.c \
	cairo-gstate.c cairo-hash.c cairo-hull.c \
	cairo-image-compositor.c cairo-image-info.c cairo-image-source.c \
	cairo-image-surface.c cairo-line.c cairo-lzw.c \
	cairo-mask-compositor.c cairo-matrix.c cairo-mempool.c \
	cairo-mesh-pattern-rasterizer.c cairo-misc.c \
	cairo-mono-scan-converter.c cairo-mutex.c cairo-no-compositor.c \
	cairo-observer.c cairo-output-stream.c cairo-paginated-surface.c \
	cairo-path-bounds.c cairo-path-fill.c cairo-path-fixed.c \
	cairo-path-in-fill.c cairo-path-stroke-boxes.c \
	cairo-path-stroke-polygon.c cairo-path-stroke-traps.c \
	cairo-path-stroke-tristrip.c cairo-path-stroke.c cairo-path.c \
	cairo-pattern.c cairo-pdf-operators.c cairo-pdf-shading.c cairo-pen.c \
	cairo-polygon-intersect.c cairo-polygon-reduce.c \
	cairo-polygon.c cairo-raster-source-pattern.c \
	cairo-recording-surface.c cairo-rectangle.c \
	cairo-rectangular-scan-converter.c cairo-region.c cairo-rtree.c \
	cairo-scaled-font-subsets.c cairo-scaled-font.c \
	cairo-shape-mask-compositor.c cairo-slope.c cairo-spans-compositor.c \
	cairo-spans.c cairo-spline.c cairo-stroke-dash.c cairo-stroke-style.c \
	cairo-surface-clipper.c cairo-surface-fallback.c \
	cairo-surface-observer.c cairo-surface-offset.c \
	cairo-surface-snapshot.c cairo-surface-subsurface.c \
	cairo-surface-wrapper.c cairo-surface.c \
	cairo-tag-attributes.c cairo-tag-stack.c \
	cairo-time.c cairo-tor-scan-converter.c cairo-tor22-scan-converter.c \
	cairo-toy-font-face.c cairo-traps-compositor.c cairo-traps.c \
	cairo-tristrip.c cairo-truetype-subset.c cairo-type1-fallback.c \
	cairo-type1-glyph-names.c cairo-type1-subset.c \
	cairo-type3-glyph-surface.c cairo-unicode.c cairo-user-font.c \
	cairo-version.c cairo-wideint.c cairo.c
# The features (meson's cairo_feature_sources): PNG, FreeType, SVG.
SRCS+=	cairo-png.c
SRCS+=	cairo-ft-font.c cairo-colr-glyph-render.c cairo-svg-glyph-render.c
SRCS+=	cairo-svg-surface.c

# What meson gives every object of the library: its config.h (here),
# _GNU_SOURCE (a project argument; strtod_l, newlocale), and the flags
# cairo's meson.build asks of the compiler - the code reads pixels through
# pointers of other types, and keeps its own symbols out of reach.
CPPFLAGS+=	-DCAIRO_COMPILATION -D_GNU_SOURCE
CPPFLAGS+=	-I${.CURDIR} -I${CAIRODIST}/src
CPPFLAGS+=	-I${DESTDIR}/usr/include/pixman-1 \
		-I${DESTDIR}/usr/include/freetype2 \
		-I${DESTDIR}/usr/include/gfxcompat
CSTD=		gnu11
COPTS+=		-fno-strict-aliasing -fno-common -fvisibility=hidden
# cairo_no_warn_c_args of src/meson.build, which cairo's own build gives
# the library: config.h's WARN_UNUSED_RESULT is __warn_unused_result__
# only because meson.build pairs it with -Wno-attributes (the attribute
# lands on functions that return nothing, and GCC says so on every use).
COPTS+=		-Wno-attributes -Wno-unused-but-set-variable \
		-Wno-missing-field-initializers -Wno-unused-parameter

# GCC 15: _cairo_scaled_glyph_lookup() memsets the glyph slot that
# _cairo_scaled_font_allocate_glyph() has just handed it, and GCC follows
# the lookup's NULL into the memset as if the allocation had not stored
# anything ("writing 232 bytes into a region of size 0"; meson's build of
# the same file says it as -Warray-bounds).
COPTS.cairo-scaled-font.c+=	${CC_WNO_STRINGOP_OVERFLOW}

INCS=		cairo.h cairo-version.h cairo-deprecated.h cairo-features.h \
		cairo-ft.h cairo-svg.h
INCSDIR=	/usr/include/cairo

.include <bsd.lib.mk>
