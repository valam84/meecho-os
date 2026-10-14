/*
 * cairo's config.h for MEECHO: what meson's checks for aarch64-elf64-minix
 * made of cairo 1.18.6 (the oracle build: port/gfx/cairo-oracle.sh), as
 * NetBSD's xsrc keeps a config.h beside its dist.
 *
 * CAIRO_HAS_PTHREAD, CAIRO_HAS_REAL_PTHREAD: cairo's locks are pthread
 * mutexes, here libmthread's (libgfxcompat's <pthread.h>), as fontconfig's.
 * HAVE_NEWLOCALE, HAVE_STRTOD_L: _cairo_strtod() reads numbers in the C
 * locale through strtod_l(3) (NetBSD's libc).
 * HAVE_FT_*: FreeType 2.15 (external/mit/freetype) draws colour glyphs,
 * COLRv1 and OT-SVG (cairo-colr-glyph-render.c, cairo-svg-glyph-render.c).
 */
#ifndef CAIRO_MEECHO_CONFIG_H
#define CAIRO_MEECHO_CONFIG_H

#define ATOMIC_OP_NEEDS_MEMORY_BARRIER 1
#define CAIRO_HAS_DLSYM 1
#define CAIRO_HAS_PTHREAD 1
#define CAIRO_HAS_REAL_PTHREAD 1
#define HAS_PIXMAN_GLYPHS 1
#define HAS_PIXMAN_r8g8b8_sRGB 1
#define HAVE_ALARM 1
#define HAVE_C11_ATOMIC_PRIMITIVES 1
#define HAVE_CLOCK_GETTIME 1
#define HAVE_CTIME_R 1
#define HAVE_DRAND48 1
#define HAVE_FCFINI 1
#define HAVE_FCINIT 1
#define HAVE_FCNTL_H 1
#define HAVE_FECLEAREXCEPT 1
#define HAVE_FEDISABLEEXCEPT 1
#define HAVE_FEENABLEEXCEPT 1
#define HAVE_FENV_H 1
#define HAVE_FLOCKFILE 1
#define HAVE_FORK 1
#define HAVE_FT_COLR_V1 1
#define HAVE_FT_LOAD_NO_SVG 1
#define HAVE_FT_SVG_DOCUMENT 1
#define HAVE_FUNLOCKFILE 1
#define HAVE_GETLINE 1
#define HAVE_GMTIME_R 1
#define HAVE_INTTYPES_H 1
#define HAVE_LIBGEN_H 1
#define HAVE_LINK 1
#define HAVE_LOCALTIME_R 1
#define HAVE_MKDIR 2
#define HAVE_MMAP 1
#define HAVE_NEWLOCALE 1
#define HAVE_POLL_H 1
#define HAVE_RAISE 1
#define HAVE_SCHED_H 1
#define HAVE_SETJMP_H 1
#define HAVE_SIGNAL_H 1
#define HAVE_STDINT_H 1
#define HAVE_STRNDUP
#define HAVE_STRTOD_L 1
#define HAVE_SYS_IOCTL_H 1
#define HAVE_SYS_MMAN_H 1
#define HAVE_SYS_POLL_H 1
#define HAVE_SYS_SOCKET_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_UN_H 1
#define HAVE_SYS_WAIT_H 1
#define HAVE_TERMIOS_H 1
#define HAVE_TIME_H 1
#define HAVE_UINT64_T 1
#define HAVE_UNISTD_H 1
#define HAVE_WAITPID 1
#define HAVE___UINT128_T 1
#define SIZEOF_INT 4
#define SIZEOF_LONG 8
#define SIZEOF_LONG_LONG 8
#define SIZEOF_SIZE_T 8
#define SIZEOF_VOID_P 8
#define WARN_UNUSED_RESULT __attribute__((__warn_unused_result__))

#endif /* CAIRO_MEECHO_CONFIG_H */
