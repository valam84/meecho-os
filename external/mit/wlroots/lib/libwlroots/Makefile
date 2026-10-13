# libwlroots-0.20: the compositor library, with the headless and Wayland
# backends and the pixman renderer - what meson builds with
# -Dbackends= -Drenderers= -Dallocators= -Dsession=disabled
# -Dxwayland=disabled, as the prototype did (port/gfx).  No DRM, libinput or
# X11 backend, no GLES2 or Vulkan renderer: meechowm brings its own screen
# and input (minix/usr.bin/meechowm).
#
# Left out with the protocol it implements: types/wlr_server_decoration.c
# (KDE's server-decoration.xml is LGPL, and is not imported).
#
# Beside this file: config.h (internal) and include/wlr/{config,version}.h,
# what meson's configure_file makes for this set of features.

.include <bsd.init.mk>

LIB=		wlroots-0.20

# Several sources share a base name (backend.c, output.c, pass.c, shm.c
# ...), and objects are named by it here: each source is built through a
# symlink named by its path, backend/multi/backend.c as
# backend-multi-backend.c.  wlroots includes everything through include/,
# never beside the source, so the symlinks change nothing for the compiler.
WLR_SRCS= \
	render/color.c \
	render/dmabuf.c \
	render/drm_format_set.c \
	render/drm_syncobj_merger.c \
	render/drm_syncobj.c \
	render/pass.c \
	render/pixel_format.c \
	render/swapchain.c \
	render/wlr_renderer.c \
	render/wlr_texture.c \
	render/dmabuf_fallback.c \
	render/pixman/pass.c \
	render/pixman/pixel_format.c \
	render/pixman/renderer.c \
	render/allocator/allocator.c \
	render/allocator/shm.c \
	render/allocator/drm_dumb.c \
	render/color_fallback.c \
	backend/backend.c \
	backend/multi/backend.c \
	backend/wayland/backend.c \
	backend/wayland/output.c \
	backend/wayland/seat.c \
	backend/wayland/pointer.c \
	backend/wayland/tablet_v2.c \
	backend/headless/backend.c \
	backend/headless/output.c \
	types/data_device/wlr_data_device.c \
	types/data_device/wlr_data_offer.c \
	types/data_device/wlr_data_source.c \
	types/data_device/wlr_drag.c \
	types/ext_image_capture_source_v1/base.c \
	types/ext_image_capture_source_v1/output.c \
	types/ext_image_capture_source_v1/foreign_toplevel.c \
	types/ext_image_capture_source_v1/scene.c \
	types/output/cursor.c \
	types/output/output.c \
	types/output/render.c \
	types/output/state.c \
	types/output/swapchain.c \
	types/scene/drag_icon.c \
	types/scene/subsurface_tree.c \
	types/scene/surface.c \
	types/scene/wlr_scene.c \
	types/scene/output_layout.c \
	types/scene/xdg_shell.c \
	types/scene/layer_shell_v1.c \
	types/seat/wlr_seat_keyboard.c \
	types/seat/wlr_seat_pointer.c \
	types/seat/wlr_seat_touch.c \
	types/seat/wlr_seat.c \
	types/tablet_v2/wlr_tablet_v2_pad.c \
	types/tablet_v2/wlr_tablet_v2_tablet.c \
	types/tablet_v2/wlr_tablet_v2_tool.c \
	types/tablet_v2/wlr_tablet_v2.c \
	types/xdg_shell/wlr_xdg_popup.c \
	types/xdg_shell/wlr_xdg_positioner.c \
	types/xdg_shell/wlr_xdg_shell.c \
	types/xdg_shell/wlr_xdg_surface.c \
	types/xdg_shell/wlr_xdg_toplevel.c \
	types/buffer/buffer.c \
	types/buffer/client.c \
	types/buffer/dmabuf.c \
	types/buffer/readonly_data.c \
	types/buffer/resource.c \
	types/wlr_alpha_modifier_v1.c \
	types/wlr_color_management_v1.c \
	types/wlr_color_representation_v1.c \
	types/wlr_compositor.c \
	types/wlr_content_type_v1.c \
	types/wlr_cursor.c \
	types/wlr_cursor_shape_v1.c \
	types/wlr_damage_ring.c \
	types/wlr_data_control_v1.c \
	types/wlr_drm.c \
	types/wlr_export_dmabuf_v1.c \
	types/wlr_ext_data_control_v1.c \
	types/wlr_ext_foreign_toplevel_list_v1.c \
	types/wlr_ext_image_copy_capture_v1.c \
	types/wlr_ext_workspace_v1.c \
	types/wlr_fixes.c \
	types/wlr_foreign_toplevel_management_v1.c \
	types/wlr_fractional_scale_v1.c \
	types/wlr_gamma_control_v1.c \
	types/wlr_idle_inhibit_v1.c \
	types/wlr_idle_notify_v1.c \
	types/wlr_input_device.c \
	types/wlr_input_method_v2.c \
	types/wlr_keyboard.c \
	types/wlr_keyboard_group.c \
	types/wlr_keyboard_shortcuts_inhibit_v1.c \
	types/wlr_layer_shell_v1.c \
	types/wlr_linux_dmabuf_v1.c \
	types/wlr_linux_drm_syncobj_v1.c \
	types/wlr_output_layer.c \
	types/wlr_output_layout.c \
	types/wlr_output_management_v1.c \
	types/wlr_output_power_management_v1.c \
	types/wlr_output_swapchain_manager.c \
	types/wlr_pointer_constraints_v1.c \
	types/wlr_pointer_gestures_v1.c \
	types/wlr_pointer.c \
	types/wlr_presentation_time.c \
	types/wlr_primary_selection_v1.c \
	types/wlr_primary_selection.c \
	types/wlr_region.c \
	types/wlr_relative_pointer_v1.c \
	types/wlr_screencopy_v1.c \
	types/wlr_security_context_v1.c \
	types/wlr_session_lock_v1.c \
	types/wlr_shm.c \
	types/wlr_single_pixel_buffer_v1.c \
	types/wlr_subcompositor.c \
	types/wlr_switch.c \
	types/wlr_tablet_pad.c \
	types/wlr_tablet_tool.c \
	types/wlr_tearing_control_v1.c \
	types/wlr_text_input_v3.c \
	types/wlr_touch.c \
	types/wlr_transient_seat_v1.c \
	types/wlr_viewporter.c \
	types/wlr_virtual_keyboard_v1.c \
	types/wlr_virtual_pointer_v1.c \
	types/wlr_xcursor_manager.c \
	types/wlr_xdg_activation_v1.c \
	types/wlr_xdg_decoration_v1.c \
	types/wlr_xdg_dialog_v1.c \
	types/wlr_xdg_foreign_v1.c \
	types/wlr_xdg_foreign_v2.c \
	types/wlr_xdg_foreign_registry.c \
	types/wlr_xdg_output_v1.c \
	types/wlr_xdg_system_bell_v1.c \
	types/wlr_xdg_toplevel_icon_v1.c \
	types/wlr_xdg_toplevel_tag_v1.c \
	util/addon.c \
	util/array.c \
	util/box.c \
	util/env.c \
	util/fd.c \
	util/global.c \
	util/log.c \
	util/matrix.c \
	util/mem.c \
	util/rect_union.c \
	util/region.c \
	util/set.c \
	util/shm.c \
	util/time.c \
	util/token.c \
	util/transform.c \
	util/utf8.c \
	util/version.c \
	xcursor/wlr_xcursor.c \
	xcursor/xcursor.c

.for _s in ${WLR_SRCS}
BUILDSYMLINKS+=	${WLRDIST}/${_s} ${_s:S,/,-,g}
SRCS+=		${_s:S,/,-,g}
.endfor

# Protocol code, made by the scanner: <name>-protocol.c and the server
# header for each, the client header too for the Wayland backend.
WLR_PROTOCOLS= \
	${WLPROTOCOLSDIST}/stable/linux-dmabuf/linux-dmabuf-v1.xml \
	${WLPROTOCOLSDIST}/stable/presentation-time/presentation-time.xml \
	${WLPROTOCOLSDIST}/stable/tablet/tablet-v2.xml \
	${WLPROTOCOLSDIST}/stable/viewporter/viewporter.xml \
	${WLPROTOCOLSDIST}/stable/xdg-shell/xdg-shell.xml \
	${WLPROTOCOLSDIST}/staging/alpha-modifier/alpha-modifier-v1.xml \
	${WLPROTOCOLSDIST}/staging/color-management/color-management-v1.xml \
	${WLPROTOCOLSDIST}/staging/color-representation/color-representation-v1.xml \
	${WLPROTOCOLSDIST}/staging/content-type/content-type-v1.xml \
	${WLPROTOCOLSDIST}/staging/cursor-shape/cursor-shape-v1.xml \
	${WLPROTOCOLSDIST}/staging/drm-lease/drm-lease-v1.xml \
	${WLPROTOCOLSDIST}/staging/ext-foreign-toplevel-list/ext-foreign-toplevel-list-v1.xml \
	${WLPROTOCOLSDIST}/staging/ext-idle-notify/ext-idle-notify-v1.xml \
	${WLPROTOCOLSDIST}/staging/ext-image-capture-source/ext-image-capture-source-v1.xml \
	${WLPROTOCOLSDIST}/staging/ext-image-copy-capture/ext-image-copy-capture-v1.xml \
	${WLPROTOCOLSDIST}/staging/ext-session-lock/ext-session-lock-v1.xml \
	${WLPROTOCOLSDIST}/staging/ext-data-control/ext-data-control-v1.xml \
	${WLPROTOCOLSDIST}/staging/ext-workspace/ext-workspace-v1.xml \
	${WLPROTOCOLSDIST}/staging/fractional-scale/fractional-scale-v1.xml \
	${WLPROTOCOLSDIST}/staging/linux-drm-syncobj/linux-drm-syncobj-v1.xml \
	${WLPROTOCOLSDIST}/staging/security-context/security-context-v1.xml \
	${WLPROTOCOLSDIST}/staging/single-pixel-buffer/single-pixel-buffer-v1.xml \
	${WLPROTOCOLSDIST}/staging/xdg-activation/xdg-activation-v1.xml \
	${WLPROTOCOLSDIST}/staging/xdg-dialog/xdg-dialog-v1.xml \
	${WLPROTOCOLSDIST}/staging/xdg-system-bell/xdg-system-bell-v1.xml \
	${WLPROTOCOLSDIST}/staging/xdg-toplevel-icon/xdg-toplevel-icon-v1.xml \
	${WLPROTOCOLSDIST}/staging/xdg-toplevel-tag/xdg-toplevel-tag-v1.xml \
	${WLPROTOCOLSDIST}/staging/xwayland-shell/xwayland-shell-v1.xml \
	${WLPROTOCOLSDIST}/staging/tearing-control/tearing-control-v1.xml \
	${WLPROTOCOLSDIST}/unstable/idle-inhibit/idle-inhibit-unstable-v1.xml \
	${WLPROTOCOLSDIST}/unstable/keyboard-shortcuts-inhibit/keyboard-shortcuts-inhibit-unstable-v1.xml \
	${WLPROTOCOLSDIST}/unstable/pointer-constraints/pointer-constraints-unstable-v1.xml \
	${WLPROTOCOLSDIST}/unstable/pointer-gestures/pointer-gestures-unstable-v1.xml \
	${WLPROTOCOLSDIST}/unstable/primary-selection/primary-selection-unstable-v1.xml \
	${WLPROTOCOLSDIST}/unstable/relative-pointer/relative-pointer-unstable-v1.xml \
	${WLPROTOCOLSDIST}/unstable/text-input/text-input-unstable-v3.xml \
	${WLPROTOCOLSDIST}/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml \
	${WLPROTOCOLSDIST}/unstable/xdg-foreign/xdg-foreign-unstable-v1.xml \
	${WLPROTOCOLSDIST}/unstable/xdg-foreign/xdg-foreign-unstable-v2.xml \
	${WLPROTOCOLSDIST}/unstable/xdg-output/xdg-output-unstable-v1.xml \
	${WLPROTOCOLSDIST}/staging/ext-transient-seat/ext-transient-seat-v1.xml \
	${WLRDIST}/protocol/drm.xml \
	${WLRDIST}/protocol/input-method-unstable-v2.xml \
	${WLRDIST}/protocol/virtual-keyboard-unstable-v1.xml \
	${WLRDIST}/protocol/wlr-data-control-unstable-v1.xml \
	${WLRDIST}/protocol/wlr-export-dmabuf-unstable-v1.xml \
	${WLRDIST}/protocol/wlr-foreign-toplevel-management-unstable-v1.xml \
	${WLRDIST}/protocol/wlr-gamma-control-unstable-v1.xml \
	${WLRDIST}/protocol/wlr-layer-shell-unstable-v1.xml \
	${WLRDIST}/protocol/wlr-output-management-unstable-v1.xml \
	${WLRDIST}/protocol/wlr-output-power-management-unstable-v1.xml \
	${WLRDIST}/protocol/wlr-screencopy-unstable-v1.xml \
	${WLRDIST}/protocol/wlr-virtual-pointer-unstable-v1.xml
.for _x in ${WLR_PROTOCOLS}
${_x:T:R}-protocol.c: ${_x}
	${_MKTARGET_CREATE}
	${TOOL_WAYLAND_SCANNER} private-code ${.ALLSRC} ${.TARGET}
${_x:T:R}-protocol.h: ${_x}
	${_MKTARGET_CREATE}
	${TOOL_WAYLAND_SCANNER} server-header ${.ALLSRC} ${.TARGET}
${_x:T:R}-client-protocol.h: ${_x}
	${_MKTARGET_CREATE}
	${TOOL_WAYLAND_SCANNER} client-header ${.ALLSRC} ${.TARGET}
SRCS+=		${_x:T:R}-protocol.c
DPSRCS+=	${_x:T:R}-protocol.h ${_x:T:R}-client-protocol.h
CLEANFILES+=	${_x:T:R}-protocol.c ${_x:T:R}-protocol.h \
		${_x:T:R}-client-protocol.h
.endfor

# What wlroots' meson.build gives every file.
CPPFLAGS+=	-D_POSIX_C_SOURCE=200809L -D_NETBSD_SOURCE \
		-DWLR_USE_UNSTABLE -DWLR_PRIVATE= \
		-DWLR_LITTLE_ENDIAN=1 -DWLR_BIG_ENDIAN=0
CPPFLAGS+=	-I. -I${.CURDIR} -I${.CURDIR}/include -I${WLRDIST}/include
CPPFLAGS+=	-I${DESTDIR}/usr/include/libdrm \
		-I${DESTDIR}/usr/include/pixman-1 \
		-I${DESTDIR}/usr/include/libepoll-shim \
		-I${DESTDIR}/usr/include/gfxcompat
COPTS+=		-std=c23
# Source paths in log messages as wlroots prints them: relative.
COPTS+=		-fmacro-prefix-map=${WLRDIST}/=

.PATH:		${.CURDIR}/include ${WLRDIST}/include
INCSDIR=	/usr/include/wlroots-0.20
INCS= \
	wlr/backend.h \
	wlr/xcursor.h \
	wlr/backend/headless.h \
	wlr/backend/interface.h \
	wlr/backend/multi.h \
	wlr/backend/wayland.h \
	wlr/interfaces/wlr_buffer.h \
	wlr/interfaces/wlr_ext_image_capture_source_v1.h \
	wlr/interfaces/wlr_keyboard.h \
	wlr/interfaces/wlr_output.h \
	wlr/interfaces/wlr_pointer.h \
	wlr/interfaces/wlr_switch.h \
	wlr/interfaces/wlr_tablet_pad.h \
	wlr/interfaces/wlr_tablet_tool.h \
	wlr/interfaces/wlr_touch.h \
	wlr/render/allocator.h \
	wlr/render/color.h \
	wlr/render/dmabuf.h \
	wlr/render/drm_format_set.h \
	wlr/render/drm_syncobj.h \
	wlr/render/interface.h \
	wlr/render/pass.h \
	wlr/render/pixman.h \
	wlr/render/swapchain.h \
	wlr/render/wlr_renderer.h \
	wlr/render/wlr_texture.h \
	wlr/types/wlr_alpha_modifier_v1.h \
	wlr/types/wlr_buffer.h \
	wlr/types/wlr_color_management_v1.h \
	wlr/types/wlr_color_representation_v1.h \
	wlr/types/wlr_compositor.h \
	wlr/types/wlr_content_type_v1.h \
	wlr/types/wlr_cursor.h \
	wlr/types/wlr_cursor_shape_v1.h \
	wlr/types/wlr_damage_ring.h \
	wlr/types/wlr_data_control_v1.h \
	wlr/types/wlr_data_device.h \
	wlr/types/wlr_drm.h \
	wlr/types/wlr_export_dmabuf_v1.h \
	wlr/types/wlr_ext_data_control_v1.h \
	wlr/types/wlr_ext_foreign_toplevel_list_v1.h \
	wlr/types/wlr_ext_image_capture_source_v1.h \
	wlr/types/wlr_ext_image_copy_capture_v1.h \
	wlr/types/wlr_ext_workspace_v1.h \
	wlr/types/wlr_fixes.h \
	wlr/types/wlr_foreign_toplevel_management_v1.h \
	wlr/types/wlr_fractional_scale_v1.h \
	wlr/types/wlr_gamma_control_v1.h \
	wlr/types/wlr_idle_inhibit_v1.h \
	wlr/types/wlr_idle_notify_v1.h \
	wlr/types/wlr_input_device.h \
	wlr/types/wlr_input_method_v2.h \
	wlr/types/wlr_keyboard.h \
	wlr/types/wlr_keyboard_group.h \
	wlr/types/wlr_keyboard_shortcuts_inhibit_v1.h \
	wlr/types/wlr_layer_shell_v1.h \
	wlr/types/wlr_linux_dmabuf_v1.h \
	wlr/types/wlr_linux_drm_syncobj_v1.h \
	wlr/types/wlr_output.h \
	wlr/types/wlr_output_layer.h \
	wlr/types/wlr_output_layout.h \
	wlr/types/wlr_output_management_v1.h \
	wlr/types/wlr_output_power_management_v1.h \
	wlr/types/wlr_output_swapchain_manager.h \
	wlr/types/wlr_pointer.h \
	wlr/types/wlr_pointer_constraints_v1.h \
	wlr/types/wlr_pointer_gestures_v1.h \
	wlr/types/wlr_presentation_time.h \
	wlr/types/wlr_primary_selection.h \
	wlr/types/wlr_primary_selection_v1.h \
	wlr/types/wlr_relative_pointer_v1.h \
	wlr/types/wlr_scene.h \
	wlr/types/wlr_screencopy_v1.h \
	wlr/types/wlr_seat.h \
	wlr/types/wlr_security_context_v1.h \
	wlr/types/wlr_session_lock_v1.h \
	wlr/types/wlr_shm.h \
	wlr/types/wlr_single_pixel_buffer_v1.h \
	wlr/types/wlr_subcompositor.h \
	wlr/types/wlr_switch.h \
	wlr/types/wlr_tablet_pad.h \
	wlr/types/wlr_tablet_tool.h \
	wlr/types/wlr_tablet_v2.h \
	wlr/types/wlr_tearing_control_v1.h \
	wlr/types/wlr_text_input_v3.h \
	wlr/types/wlr_touch.h \
	wlr/types/wlr_transient_seat_v1.h \
	wlr/types/wlr_viewporter.h \
	wlr/types/wlr_virtual_keyboard_v1.h \
	wlr/types/wlr_virtual_pointer_v1.h \
	wlr/types/wlr_xcursor_manager.h \
	wlr/types/wlr_xdg_activation_v1.h \
	wlr/types/wlr_xdg_decoration_v1.h \
	wlr/types/wlr_xdg_dialog_v1.h \
	wlr/types/wlr_xdg_foreign_registry.h \
	wlr/types/wlr_xdg_foreign_v1.h \
	wlr/types/wlr_xdg_foreign_v2.h \
	wlr/types/wlr_xdg_output_v1.h \
	wlr/types/wlr_xdg_shell.h \
	wlr/types/wlr_xdg_system_bell_v1.h \
	wlr/types/wlr_xdg_toplevel_icon_v1.h \
	wlr/types/wlr_xdg_toplevel_tag_v1.h \
	wlr/util/addon.h \
	wlr/util/box.h \
	wlr/util/edges.h \
	wlr/util/log.h \
	wlr/util/region.h \
	wlr/util/transform.h \
	wlr/config.h \
	wlr/version.h

.include <bsd.lib.mk>
