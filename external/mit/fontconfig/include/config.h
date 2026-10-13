/*
 * fontconfig's config.h for MEECHO: meson-config.h as meson's checks made
 * it for the prototype (port/gfx), with the paths of NetBSD's layout -
 * configuration in /etc/fonts, its templates in /etc/fonts/conf.avail
 * (external/mit/fontconfig/etc) - and then config-fixups.h, as meson's
 * config.h has it.
 *
 * HAVE_PTHREAD: fontconfig's locks are pthread mutexes, here libmthread's
 * (libgfxcompat's <pthread.h>).
 */
#ifndef FONTCONFIG_MEECHO_CONFIG_H
#define FONTCONFIG_MEECHO_CONFIG_H

#define ALIGNOF_DOUBLE 8
#define ALIGNOF_VOID_P 8
#define CONFIGDIR "/etc/fonts/conf.d"
#define ENABLE_FREETYPE 1
#define EXEEXT
#define FC_CACHEDIR "/var/cache/fontconfig"
#define FC_DEFAULT_FONTS "\t<dir>/usr/share/fonts</dir>\n"
#define FC_FONTPATH ""
#define FC_GPERF_SIZE_T size_t
#define FC_TEMPLATEDIR "/etc/fonts/conf.avail"
#define FC_VERSION_MAJOR 2
#define FC_VERSION_MICRO 3
#define FC_VERSION_MINOR 18
#define FLEXIBLE_ARRAY_MEMBER /**/
#define FONTCONFIG_PATH "/etc/fonts"
#undef FREETYPE_PCF_LONG_FAMILY_NAMES
#define GETTEXT_PACKAGE "fontconfig"
#define HAVE_C99_VSNPRINTF 1
#define HAVE_DIRENT_H 1
#define HAVE_DLFCN_H 1
#define HAVE_FCNTL_H 1
#define HAVE_FSTATFS 1
#define HAVE_FSTATVFS 1
#define HAVE_FT_DONE_MM_VAR 1
#define HAVE_FT_GET_BDF_PROPERTY 1
#define HAVE_FT_GET_PS_FONT_INFO 1
#define HAVE_FT_GET_X11_FONT_FORMAT 1
#define HAVE_FT_HAS_PS_GLYPH_NAMES 1
#define HAVE_GETOPT 1
#define HAVE_GETOPT_LONG 1
#define HAVE_GETPAGESIZE 1
#define HAVE_GETPID 1
#define HAVE_GETPROGNAME 1
#define HAVE_INTEL_ATOMIC_PRIMITIVES 1
#define HAVE_INTTYPES_H 1
#define HAVE_LINK 1
#define HAVE_LOCALTIME_R 1
#define HAVE_LRAND48 1
#define HAVE_LSTAT 1
#define HAVE_MKDTEMP 1
#define HAVE_MKOSTEMP 1
#define HAVE_MKSTEMP 1
#define HAVE_MMAP 1
#define HAVE_POSIX_FADVISE 1
#define HAVE_PTHREAD 1
#define HAVE_RAND 1
#define HAVE_RANDOM 1
#define HAVE_RAND_R 1
#define HAVE_READLINK 1
#define HAVE_STDATOMIC_PRIMITIVES 1
#define HAVE_STDINT_H 1
#define HAVE_STDIO_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRDUP 1
#define HAVE_STRERROR 1
#define HAVE_STRERROR_R 1
#define HAVE_STRINGS_H 1
#define HAVE_STRING_H 1
#define HAVE_STRUCT_DIRENT_D_TYPE 1
#define HAVE_STRUCT_STATVFS_F_FSTYPENAME 1
#define HAVE_STRUCT_STAT_ST_MTIM 1
#define HAVE_SYMLINK 1
#define HAVE_SYS_MOUNT_H 1
#define HAVE_SYS_PARAM_H 1
#define HAVE_SYS_STATFS_H 1
#define HAVE_SYS_STATVFS_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_TIME_H 1
#define HAVE_UNISTD_H 1
#define HAVE_VASPRINTF 1
#define HAVE_VASPRINTF_L 1
#define HAVE_VPRINTF 1
#define HAVE_VSNPRINTF 1
#define HAVE_VSPRINTF 1
#define HAVE_WCHAR_H 1
#define HAVE__VSNPRINTF_L 1
#define PACKAGE_BUGREPORT "https://gitlab.freedesktop.org/fontconfig/fontconfig/issues/new"
#define PACKAGE_NAME "fontconfig"
#define PACKAGE_STRING "fontconfig 2.18.3"
#define PACKAGE_TARNAME "fontconfig"
#define PACKAGE_URL ""
#define PACKAGE_VERSION "2.18.3"
#define SIZEOF_VOID_P 8
#define USE_ICONV 0
#define WORDS_BIGENDIAN 0
#define _GNU_SOURCE

#include "config-fixups.h"

#endif
