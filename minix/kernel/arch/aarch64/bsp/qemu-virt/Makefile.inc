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

SRCS+=	virt_serial.c virt_reset.c
