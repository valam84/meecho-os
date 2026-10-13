# wayland-protocols 1.49 (stage 11): the protocols' enums as C headers,
# <wayland-protocols/NAME-enum.h> - wlroots' public headers include them.
# One per protocol meson installs headers for, made by the scanner as
# meson makes them (enum-header --strict).
#
# The XML itself stays in the tree, where the stack's reachovers read it;
# it is not installed (/usr/share/wayland-protocols): nothing on the
# machine builds Wayland code yet.

.include <bsd.init.mk>

WLPROTOCOLSDIST=	${NETBSDSRCDIR}/external/mit/wayland-protocols/dist

WLP_XML= \
	staging/alpha-modifier/alpha-modifier-v1.xml \
	staging/color-management/color-management-v1.xml \
	staging/color-representation/color-representation-v1.xml \
	staging/commit-timing/commit-timing-v1.xml \
	staging/content-type/content-type-v1.xml \
	staging/cursor-shape/cursor-shape-v1.xml \
	staging/drm-lease/drm-lease-v1.xml \
	staging/ext-background-effect/ext-background-effect-v1.xml \
	staging/ext-data-control/ext-data-control-v1.xml \
	staging/ext-foreign-toplevel-list/ext-foreign-toplevel-list-v1.xml \
	staging/ext-idle-notify/ext-idle-notify-v1.xml \
	staging/ext-image-capture-source/ext-image-capture-source-v1.xml \
	staging/ext-image-copy-capture/ext-image-copy-capture-v1.xml \
	staging/ext-session-lock/ext-session-lock-v1.xml \
	staging/ext-transient-seat/ext-transient-seat-v1.xml \
	staging/ext-workspace/ext-workspace-v1.xml \
	staging/fifo/fifo-v1.xml \
	staging/fractional-scale/fractional-scale-v1.xml \
	unstable/fullscreen-shell/fullscreen-shell-unstable-v1.xml \
	unstable/idle-inhibit/idle-inhibit-unstable-v1.xml \
	unstable/input-method/input-method-unstable-v1.xml \
	unstable/input-timestamps/input-timestamps-unstable-v1.xml \
	unstable/keyboard-shortcuts-inhibit/keyboard-shortcuts-inhibit-unstable-v1.xml \
	unstable/linux-dmabuf/linux-dmabuf-unstable-v1.xml \
	stable/linux-dmabuf/linux-dmabuf-v1.xml \
	staging/linux-drm-syncobj/linux-drm-syncobj-v1.xml \
	unstable/linux-explicit-synchronization/linux-explicit-synchronization-unstable-v1.xml \
	unstable/pointer-constraints/pointer-constraints-unstable-v1.xml \
	unstable/pointer-gestures/pointer-gestures-unstable-v1.xml \
	staging/pointer-warp/pointer-warp-v1.xml \
	stable/presentation-time/presentation-time.xml \
	unstable/primary-selection/primary-selection-unstable-v1.xml \
	unstable/relative-pointer/relative-pointer-unstable-v1.xml \
	staging/security-context/security-context-v1.xml \
	staging/single-pixel-buffer/single-pixel-buffer-v1.xml \
	unstable/tablet/tablet-unstable-v1.xml \
	unstable/tablet/tablet-unstable-v2.xml \
	stable/tablet/tablet-v2.xml \
	staging/tearing-control/tearing-control-v1.xml \
	unstable/text-input/text-input-unstable-v1.xml \
	unstable/text-input/text-input-unstable-v3.xml \
	stable/viewporter/viewporter.xml \
	staging/xdg-activation/xdg-activation-v1.xml \
	unstable/xdg-decoration/xdg-decoration-unstable-v1.xml \
	staging/xdg-dialog/xdg-dialog-v1.xml \
	unstable/xdg-foreign/xdg-foreign-unstable-v1.xml \
	unstable/xdg-foreign/xdg-foreign-unstable-v2.xml \
	unstable/xdg-output/xdg-output-unstable-v1.xml \
	staging/xdg-session-management/xdg-session-management-v1.xml \
	stable/xdg-shell/xdg-shell.xml \
	unstable/xdg-shell/xdg-shell-unstable-v5.xml \
	unstable/xdg-shell/xdg-shell-unstable-v6.xml \
	staging/xdg-system-bell/xdg-system-bell-v1.xml \
	staging/xdg-toplevel-drag/xdg-toplevel-drag-v1.xml \
	staging/xdg-toplevel-icon/xdg-toplevel-icon-v1.xml \
	staging/xdg-toplevel-tag/xdg-toplevel-tag-v1.xml \
	unstable/xwayland-keyboard-grab/xwayland-keyboard-grab-unstable-v1.xml \
	staging/xwayland-shell/xwayland-shell-v1.xml \
	experimental/xx-cutouts/xx-cutouts-v1.xml \
	experimental/xx-input-method/xx-input-method-v2.xml \
	experimental/xx-keyboard-filter/xx-keyboard-filter-v1.xml \
	experimental/xx-session-management/xx-session-management-v1.xml \
	experimental/xx-text-input/xx-text-input-v3.xml \
	experimental/xx-zones/xx-zones-v1.xml

.for _x in ${WLP_XML}
${_x:T:R}-enum.h: ${WLPROTOCOLSDIST}/${_x}
	${_MKTARGET_CREATE}
	${TOOL_WAYLAND_SCANNER} --strict enum-header ${.ALLSRC} ${.TARGET}
INCS+=		${_x:T:R}-enum.h
CLEANFILES+=	${_x:T:R}-enum.h
.endfor

INCSDIR=	/usr/include/wayland-protocols

.include <bsd.prog.mk>
