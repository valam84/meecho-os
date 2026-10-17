.include <bsd.own.mk>

.if ${MACHINE_ARCH} == "earm"
SUBDIR+=	usbd
.endif # ${MACHINE_ARCH} == "earm"

SUBDIR+=	usb_storage usb_hub
# The xHCI of the RK3566/RK3568, which is where modern USB on this port
# lives: one controller for every speed.  aarch64 only - it is written for
# a DWC3 on a memory-mapped bus with a device tree to find it, and the two
# 32-bit ports have neither.
.if ${MACHINE_ARCH} == "aarch64"
SUBDIR+=	xhci
.endif

# A keyboard, through the boot protocol.  Only where something above it
# turns key events into characters: tty on aarch64 does since the screen
# console (arch/aarch64/keyboard.c); i386 has pckbd for its keyboards and
# earm no consumer at all.
.if ${MACHINE_ARCH} == "aarch64"
SUBDIR+=	usb_kbd
.endif

# A mouse, through the boot protocol.  Its events go to the readers of
# /dev/mousemux - the compositor of stage 11 - and the same reasoning
# keeps it to aarch64: nothing on the other two reads them from USB.
.if ${MACHINE_ARCH} == "aarch64"
SUBDIR+=	usb_mouse
.endif

# CDC Ethernet, a network interface on USB: an adapter, or the Mi Pad 4 on
# a cable.  aarch64 only, for xhci's reason: it is the only controller
# that waits a bulk IN transfer for a frame (USB_URB_WAITS) and tells its
# endpoints about an alternate setting.
.if ${MACHINE_ARCH} == "aarch64"
SUBDIR+=	usb_ecm
.endif


.include <bsd.subdir.mk>
