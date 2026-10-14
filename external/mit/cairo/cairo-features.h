/*
 * cairo-features.h for MEECHO (installed with cairo's headers): the
 * surfaces and font back ends this build of cairo has, as meson's build
 * of cairo 1.18.6 for aarch64-elf64-minix wrote it (port/gfx/cairo-oracle.sh).
 * The image surface draws with pixman; PNG through external/mit/libpng,
 * and the SVG surface comes with it; fonts through FreeType and fontconfig.
 * No X11, no GL, no PostScript, PDF or script surface (they want zlib in
 * cairo's build, and nothing here asks for them).
 */
#ifndef CAIRO_FEATURES_H
#define CAIRO_FEATURES_H

#define CAIRO_HAS_FC_FONT 1
#define CAIRO_HAS_FT_FONT 1
#define CAIRO_HAS_IMAGE_SURFACE 1
#define CAIRO_HAS_MIME_SURFACE 1
#define CAIRO_HAS_OBSERVER_SURFACE 1
#define CAIRO_HAS_PNG_FUNCTIONS 1
#define CAIRO_HAS_RECORDING_SURFACE 1
#define CAIRO_HAS_SVG_SURFACE 1
#define CAIRO_HAS_USER_FONT 1

#endif /* CAIRO_FEATURES_H */
