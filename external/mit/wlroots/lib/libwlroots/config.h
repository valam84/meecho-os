/*
 * wlroots' internal config.h for MEECHO: what meson's configure_file made
 * for the prototype (port/gfx).  HAVE_EVENTFD is 0 because meson looks
 * for <sys/eventfd.h> in the default include path, and libepoll-shim's is
 * in its own; it only decides whether DRM sync objects can be waited on,
 * and there is no DRM device here.
 */
#ifndef WLR_MEECHO_INTERNAL_CONFIG_H
#define WLR_MEECHO_INTERNAL_CONFIG_H

#define HAVE_EGL 0
#define HAVE_EVENTFD 0
#define HAVE_LIBLIFTOFF 0
#define HAVE_LINUX_SYNC_FILE 0
#define HAVE_XCB_ERRORS 0
#define ICONDIR "/usr/share/icons"

#endif
