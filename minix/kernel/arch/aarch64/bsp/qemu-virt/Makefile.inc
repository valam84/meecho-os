#
# BSP for QEMU's virt machine.
#
# What is left here is what is genuinely board support, and the package has
# been shrinking as the real second board arrived. The BCM2711 this file once
# expected is not it: the target is the BIGTREETECH CB2, whose RK3566 shares
# neither the console part nor the interrupt controller version with QEMU.
# That settled the shape - anything the device tree reports is asked for at
# run time and lives in arch/aarch64, so that one kernel runs on both
# machines, and only what the tree cannot say stays here.
#

.PATH: ${.CURDIR}/arch/${MACHINE_ARCH}/bsp/qemu-virt

# Neither the interrupt controller nor the console is here any more. The GIC
# was virt_intr.c and is now arch/aarch64/gic*.c; the PL011 was virt_serial.c
# and is now arch/aarch64/pl011.c, chosen by serial.c. The board decides
# neither the GIC version nor which UART the console is wired to - the tree
# reports both, and this one machine even reports either GIC version
# depending on QEMU's gic-version=.
SRCS+=	virt_reset.c virt_timer.c
