#
# BSP for Broadcom hardware (BCM2711 / Raspberry Pi 4 / Compute Module 4)
#

.PATH: ${.CURDIR}/arch/${MACHINE_ARCH}/bsp/broadcom

# These objects are also linked into the unpaged part of the kernel, which
# runs before the MMU is enabled. Serial is needed so that early boot can
# print, reset so that an early panic can still reboot the board.
.for unpaged_obj in bcm2711_serial.o bcm2711_reset.o
BSP_OBJS_UNPAGED += ${unpaged_obj}
.endfor

SRCS+=	bcm2711_init.c bcm2711_serial.c bcm2711_timer.c bcm2711_padconf.c \
	bcm2711_intr.c bcm2711_reset.c
