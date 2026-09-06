#!/usr/bin/env bash
set -e

#
# Assemble a bootable evbarm64 RAM image: a flat kernel and a boot archive
# holding the twelve boot images, ready for "qemu-system-aarch64 -kernel
# ... -initrd ...".
#
# This is the aarch64 counterpart of x86_ramimage.sh, but it is not built the
# same way, because the two "root in RAM" setups are not the same thing:
#
#   x86_ramimage.sh extracts the whole minix-base set into a work directory
#   and turns that into the image, so the root it boots carries the real
#   /etc/rc and the real userland.  It also has a boot loader to hand the
#   kernel its modules.
#
#   Here the root is the small ramdisk built by
#   minix/drivers/storage/ramdisk, linked into the memory driver, and there
#   is no boot loader: the firmware hands over one flat image and one initrd
#   range, so the twelve boot images travel in a boot archive of our own
#   (<machine/bootarchive.h>, mkbootarchive(8)) which the kernel unpacks in
#   pre_init().
#
# Both boot arguments below are required, and both fail quietly if left out:
#
#   bootramdisk=1   tells /etc/rc that the root it was handed is the root it
#                   keeps.  Without it the script looks for a disk it has no
#                   driver for.
#   console=tty00   points /dev/console at the serial line.  Without it the
#                   console is minor 0, which is the video console this
#                   machine does not have, and open() answers ENXIO.
#
# Do NOT add hz= without changing DEFAULT_HZ in <machine/archconst.h> to
# match: the boot argument is read by the kernel, but the fallback beside it
# is also read by sys_hz() in libsys, and the two disagreeing is silent.
#
# Environment, all overridable:
#
#   OBJ                 object directory of the build (required in practice;
#                       the default follows the in-tree build.sh layout)
#   DESTDIR, CROSS_TOOLS as for the other release scripts
#   EXTERNAL_TOOLCHAIN  where the aarch64-elf64-minix- toolchain lives, if
#                       the build used one; OBJCOPY overrides it outright
#   BOOTARGS            kernel command line
#   QEMU, QEMU_CPU, QEMU_MEM, QEMU_TIMEOUT
#

#
# Source settings if present
#
: ${SETTINGS_MINIX=.settings}
if [ -f "${SETTINGS_MINIX}" ]
then
	echo "Sourcing settings from ${SETTINGS_MINIX}"
	cat ${SETTINGS_MINIX} | sed "s,^,CONTENT ,g"
	. ${SETTINGS_MINIX}
fi

: ${ARCH=evbarm64-el}
: ${TOOLCHAIN_TRIPLET=aarch64-elf64-minix-}
: ${BUILDSH=build.sh}

: ${BOOTARGS="bootramdisk=1 console=tty00"}

: ${QEMU=qemu-system-aarch64}
: ${QEMU_CPU=cortex-a72}
: ${QEMU_MEM=512}
# Empty means "run until the user quits".  The kernel parks on wfi when it is
# done, so QEMU never exits by itself; a scripted run wants a timeout here.
: ${QEMU_TIMEOUT=}

usage()
{
	cat >&2 <<EOF
usage: $0 [-b] [-r] [-2]
	-b  build the ramdisk image and the memory driver first
	-r  run QEMU on the result
	-2  enter at EL2, where U-Boot leaves a kernel on a real board
	    (implies -r)
	-3  give the machine a GICv3 instead of the default GICv2, which is
	    what the target board has (implies -r)

The two run flags combine, and all four combinations are worth checking: the
entry level and the GIC version are independent, and each has code of its own
- the drop from EL2 in head.S enables the GICv3 system registers for EL1, so
a mistake there shows up in one combination only.
EOF
	exit 1
}

do_build=0
do_run=0
el2=0
gicv3=0
while getopts "br23h" c
do
	case "$c" in
	b)	do_build=1 ;;
	r)	do_run=1 ;;
	2)	do_run=1; el2=1 ;;
	3)	do_run=1; gicv3=1 ;;
	*)	usage ;;
	esac
done

if [ ! -f ${BUILDSH} ]
then
	echo "Please invoke me from the root source dir, where ${BUILDSH} is."
	exit 1
fi

# set up the same directory variables the other release scripts use
. releasetools/image.defaults

: ${NBMAKE=${CROSS_TOOLS}/nbmake-${ARCH}}
: ${MKBOOTARCHIVE=${CROSS_TOOLS}/nbmkbootarchive}

#
# objcopy is not one of the tools NetBSD's build installs under CROSS_TOOLS
# when the build uses an external toolchain, which this port does, so look
# for it where that toolchain is before falling back to the in-tree prefix
# and then to $PATH.
#
if [ -z "${OBJCOPY:-}" ]
then
	if [ -n "${EXTERNAL_TOOLCHAIN:-}" ]
	then
		OBJCOPY=${EXTERNAL_TOOLCHAIN}/bin/${TOOLCHAIN_TRIPLET}objcopy
	elif [ -x "${CROSS_PREFIX}objcopy" ]
	then
		OBJCOPY=${CROSS_PREFIX}objcopy
	else
		OBJCOPY=${TOOLCHAIN_TRIPLET}objcopy
	fi
fi

KERNEL=${OBJ}/minix/kernel/kernel
KERNEL_BIN=${WORK_DIR}/kernel.bin
ARCHIVE=${WORK_DIR}/boot.mba

#
# Where each boot image is built.  The names come from the kernel, not from
# here: see boot_images() below.
#
image_path()
{
	case "$1" in
	ds)	echo minix/servers/ds/ds ;;
	rs)	echo minix/servers/rs/rs ;;
	pm)	echo minix/servers/pm/pm ;;
	sched)	echo minix/servers/sched/sched ;;
	vfs)	echo minix/servers/vfs/vfs ;;
	memory)	echo minix/drivers/storage/memory/memory ;;
	tty)	echo minix/drivers/tty/tty/tty ;;
	mib)	echo minix/servers/mib/mib ;;
	vm)	echo minix/servers/vm/vm ;;
	pfs)	echo minix/fs/pfs/pfs ;;
	mfs)	echo minix/fs/mfs/mfs ;;
	init)	echo sbin/init/init ;;
	*)	return 1 ;;
	esac
}

#
# The list of boot images is the kernel's, read out of the table the kernel
# checks the archive against.  Its first entries are the kernel's own tasks,
# which are not loaded from anywhere; the loadable ones are exactly those
# with an endpoint number, so the _PROC_NR suffix is the test.
#
# Reading it here rather than repeating it means a thirteenth boot image
# fails this script with a name, instead of failing the boot with a count.
#
boot_images()
{
	sed -n 's/^[ 	]*{[A-Za-z0-9_]*_PROC_NR,[ 	]*"\([A-Za-z0-9_]*\)".*/\1/p' \
		minix/kernel/table.c
}

if [ ${do_build} -eq 1 ]
then
	echo "Building the ramdisk image and the memory driver..."
	${NBMAKE} -C minix/drivers/storage/ramdisk dependall
	${NBMAKE} -C minix/drivers/storage/memory dependall
fi

mkdir -p ${WORK_DIR}

echo "Converting the kernel to a flat image..."
# QEMU and U-Boot hand a device tree to a flat image with an arm64 header,
# and nothing at all to an ELF, so the kernel travels flat.  head.S carries
# the header and declares the load offset.
if [ ! -f "${KERNEL}" ]
then
	echo "$0: no kernel at ${KERNEL}" >&2
	echo "Build it with: ${NBMAKE} -C minix/kernel dependall" >&2
	exit 1
fi
${OBJCOPY} -O binary "${KERNEL}" "${KERNEL_BIN}"

echo "Collecting the boot images..."
mods=""
for name in $(boot_images)
do
	rel=$(image_path "${name}") || {
		echo "$0: minix/kernel/table.c wants a boot image called" \
			"\"${name}\", and image_path() does not know where" \
			"it is built" >&2
		exit 1
	}
	file=${OBJ}/${rel}
	if [ ! -f "${file}" ]
	then
		echo "$0: missing boot image ${file}" >&2
		echo "Build it with: ${NBMAKE} -C $(dirname ${rel}) dependall" >&2
		exit 1
	fi
	# name=file, so the archive carries the name the kernel looks up
	# rather than whatever the file happens to be called.
	mods="${mods} ${name}=${file}"
done

echo "Writing the boot archive..."
${MKBOOTARCHIVE} -o "${ARCHIVE}" ${mods}

virt=virt
if [ ${el2} -eq 1 ]
then
	virt="${virt},virtualization=on"
fi
if [ ${gicv3} -eq 1 ]
then
	virt="${virt},gic-version=3"
fi

cmd="${QEMU} -M ${virt} -cpu ${QEMU_CPU} -m ${QEMU_MEM}"
cmd="${cmd} -display none -serial stdio -net none"
cmd="${cmd} -kernel ${KERNEL_BIN} -initrd ${ARCHIVE}"
cmd="${cmd} -append \"${BOOTARGS}\""
if [ -n "${QEMU_TIMEOUT}" ]
then
	cmd="timeout ${QEMU_TIMEOUT} ${cmd}"
fi

echo ""
echo "RAM image in ${WORK_DIR}:"
ls -l "${KERNEL_BIN}" "${ARCHIVE}"
echo ""
echo "To boot it:"
echo "${cmd}"
echo ""
echo "virtualization=on enters at EL2, where U-Boot leaves a kernel on a real"
echo "board; gic-version=3 gives the interrupt controller the target board"
echo "has.  Both are independent, and all four combinations want checking."
echo ""

if [ ${do_run} -eq 1 ]
then
	eval "${cmd}"
fi
