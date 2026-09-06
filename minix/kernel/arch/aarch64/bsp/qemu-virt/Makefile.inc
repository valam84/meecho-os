#
# BSP for QEMU's virt machine.
#
# One board is built in, the way ARM builds in bsp/ti: which board this is
# becomes a variable when there is a second one, and the second one is the
# BCM2711. Its devices are largely the same parts at different addresses -
# the console is a PL011 on both - so what moves into a variable then is the
# address map, not the drivers.
#

.PATH: ${.CURDIR}/arch/${MACHINE_ARCH}/bsp/qemu-virt

# No interrupt controller here any more. The GIC was virt_intr.c and is now
# arch/aarch64/gic*.c, because it is not board support: the board does not
# decide the version, the device tree reports it, and this one machine
# reports either.
SRCS+=	virt_serial.c virt_reset.c virt_timer.c
