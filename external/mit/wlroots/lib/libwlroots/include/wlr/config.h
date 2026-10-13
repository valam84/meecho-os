#ifndef WLR_CONFIG_H
#define WLR_CONFIG_H

/*
 * wlroots' <wlr/config.h> for MEECHO, as meson makes it from config.h.in
 * for the features built here: the headless and Wayland backends and the
 * pixman renderer, nothing else (external/mit/wlroots/lib/libwlroots).
 */

#define WLR_HAS_DRM_BACKEND 0
#define WLR_HAS_LIBINPUT_BACKEND 0
#define WLR_HAS_X11_BACKEND 0

#define WLR_HAS_GLES2_RENDERER 0
#define WLR_HAS_VULKAN_RENDERER 0

#define WLR_HAS_GBM_ALLOCATOR 0
#define WLR_HAS_UDMABUF_ALLOCATOR 0

#define WLR_HAS_XWAYLAND 0

#define WLR_HAS_SESSION 0

#define WLR_HAS_COLOR_MANAGEMENT 0

#endif
