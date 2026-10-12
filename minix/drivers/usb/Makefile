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


.include <bsd.subdir.mk>
