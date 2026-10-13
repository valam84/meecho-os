/*
 * libxkbcommon's config.h for MEECHO: what meson's checks found of the
 * system when it built the prototype (port/gfx), kept by hand from then
 * on.  The keymap data is xkeyboard-config's (external/mit/xkeyboard-config),
 * installed under /usr/share/X11/xkb.
 */
#ifndef XKBCOMMON_MEECHO_CONFIG_H
#define XKBCOMMON_MEECHO_CONFIG_H

#define DEFAULT_XKB_LAYOUT "us"
#define DEFAULT_XKB_MODEL "pc105"
#define DEFAULT_XKB_OPTIONS NULL
#define DEFAULT_XKB_RULES "evdev"
#define DEFAULT_XKB_VARIANT NULL
#define DFLT_XKB_CONFIG_EXTRA_PATH "/etc/xkb"
#define DFLT_XKB_CONFIG_ROOT "/usr/share/X11/xkb"
#define DFLT_XKB_LEGACY_ROOT "/usr/share/X11/xkb"
#define EXIT_INVALID_USAGE 2
#define HAVE_ASPRINTF 1
#define HAVE_DIRENT_H 1
#define HAVE_MKOSTEMP 1
#define HAVE_MMAP 1
#define HAVE_OPEN_MEMSTREAM 1
#define HAVE_REAL_PATH 1
#define HAVE_STRNDUP 1
#define HAVE_UNISTD_H 1
#define HAVE_XKB_EXTENSIONS_DIRECTORIES 1
#define HAVE___BUILTIN_EXPECT 1
#define LIBXKBCOMMON_TOOL_PATH "/usr/libexec/xkbcommon"
#define LIBXKBCOMMON_VERSION "1.13.2"
#define XLOCALEDIR "/usr/share/X11/locale"
#define _GNU_SOURCE 1

#endif
