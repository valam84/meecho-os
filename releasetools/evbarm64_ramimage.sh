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

# Chosen once the options are read, unless given: the root is the ramdisk
# ("bootramdisk=1"), or with -d the disk ("rootdevname=c0d0").
: ${BOOTARGS=}
# The disk image -d makes, and its size.  Its contents are the whole
# userland the tree installed into DESTDIR, which is some sixty
# megabytes of static binaries, plus room to write in.
: ${DISK_MB=512}

: ${QEMU=qemu-system-aarch64}
: ${QEMU_CPU=cortex-a72}
: ${QEMU_MEM=512}
# How many cores the machine has. The kernel finds them in the device tree
# and starts them itself; four is what the target board has.
: ${QEMU_SMP=1}
# Empty means "run until the user quits".  The kernel parks on wfi when it is
# done, so QEMU never exits by itself; a scripted run wants a timeout here.
: ${QEMU_TIMEOUT=}

usage()
{
	cat >&2 <<EOF
usage: $0 [-b] [-d] [-n] [-r] [-2] [-3]
	-b  build the ramdisk image and the memory driver first
	-d  make a disk image from the ramdisk's contents and boot with the
	    root on it, over virtio-blk, instead of on the ramdisk (implies -r)
	-n  assemble everything asked for, but do not run QEMU.  This is how
	    a release is built: -d makes the disk image, -n keeps it from
	    being booted right away.
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
disk=0
el2=0
gicv3=0
no_run=0
while getopts "bdnr23h" c
do
	case "$c" in
	b)	do_build=1 ;;
	d)	do_run=1; disk=1 ;;
	n)	no_run=1 ;;
	r)	do_run=1 ;;
	2)	do_run=1; el2=1 ;;
	3)	do_run=1; gicv3=1 ;;
	*)	usage ;;
	esac
done

# -n is read after the loop on purpose: it has to override the flags that
# imply a run, whatever order they were given in.
if [ ${no_run} -eq 1 ]
then
	do_run=0
fi

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

#
# The disk.  Its contents are the ramdisk's, read from the proto the ramdisk
# build generated, with one substitution: /etc/rc.  The ramdisk's rc is the
# script that starts the disk driver and mounts the disk over /, and it
# ends by handing over to the /etc/rc it finds there - which on an
# installed system is the NetBSD rc infrastructure, and here, until that
# userland is in the image, is the few lines below.
#
# Whole-disk, no partition table: the file system starts at byte 0 and the
# root device is c0d0 itself.
#
DISK=${WORK_DIR}/disk.img
if [ ${disk} -eq 1 ]
then
	RAMDISK_OBJ=${OBJ}/minix/drivers/storage/ramdisk
	: ${MKFSMFS=${CROSS_TOOLS}/nbmkfs.mfs}

	if [ ! -f "${RAMDISK_OBJ}/proto.gen" ]
	then
		echo "$0: no ${RAMDISK_OBJ}/proto.gen; build the ramdisk" \
			"first (-b)" >&2
		exit 1
	fi

	# An existing image is kept, so that what a run wrote is there for
	# the next one - that is what a disk is for - unless the ramdisk it
	# was made from has been rebuilt since, or DISK_FRESH is set.
	if [ -f "${DISK}" ] && [ -z "${DISK_FRESH:-}" ] &&
	   [ ! "${RAMDISK_OBJ}/image" -nt "${DISK}" ]
	then
		echo "Keeping the disk image ${DISK} (DISK_FRESH=1 remakes it)"
	else
	echo "Writing the disk image (${DISK_MB} MB)..."
	cat > "${WORK_DIR}/rc.disk" <<'END_RC'
#!/bin/sh
# /etc/rc of the disk root.  The ramdisk's rc has started the disk driver,
# mounted this file system over / and mounted procfs; init runs this, and
# when it returns, starts the sessions listed in /etc/ttys.  Everything
# but the network is brought up by the kernel and RS before this runs.
PATH=/sbin:/usr/sbin:/bin:/usr/bin
export PATH

# Маска прав по умолчанию: её не ставит никто (см. комментарий в
# etc/profile), и без этой строки службы, запущенные отсюда, - в том
# числе sshd - создают файлы доступными на запись всем.
umask 022

echo "Root is on `sysenv rootdevname`."

# The source of randomness.  Nothing before this point needs one, and
# several things after it do - the first being the secret behind TCP
# initial sequence numbers, further along ssh.
minix-service up /service/random -dev /dev/random ||
    echo "WARNING: no random device"

# Локальные сокеты (AF_UNIX). Их даёт отдельная служба, и без неё
# socketpair(2) отказывает с ENOENT - VFS просто некому передать домен
# LOCAL. Нашлось на sshd: он заводит socketpair, чтобы разговаривать со
# своим sshd-session, и без него соединение рвётся сразу после установки
# TCP - "reexec socketpair: No such file or directory" в его отладке и
# "kex_exchange_identification: Connection reset" у клиента.
minix-service up /service/uds ||
    echo "WARNING: no local (AF_UNIX) sockets"

# Псевдотерминалы. Интерактивной сессии по ssh без них не будет: шеллу
# нужен управляющий терминал, а openpty(3) берёт его у этого драйвера.
minix-service up /service/pty -dev /dev/ptyp0 ||
    echo "WARNING: no pseudo terminals"

# Networking (8.4).  The driver first: LWIP watches DS for "drv.net.*" and
# would pick up a driver started later just as well, but starting it first
# means the interface is there by the time this script configures it.
#
# Which driver, the same way the disk says which one it has: the machine is
# named in the boot arguments rather than guessed at.  QEMU has a virtio
# transport, the CB2 has a Synopsys controller behind Rockchip glue, and a
# driver that does not find its node in the device tree fails loudly rather
# than quietly doing nothing.
if sysenv netdrv >/dev/null
then	netdrv="`sysenv netdrv`"
else	netdrv=virtio_net
fi

if [ -x "/service/$netdrv" ]
then
	minix-service up "/service/$netdrv" -label "${netdrv}_0" -args instance=0 || echo "WARNING: no network driver"
	minix-service up /service/lwip -dev /dev/bpf || echo "WARNING: no network stack"

	# The interface is looked for in the list rather than named: the
	# name LWIP gives an ethernet interface comes from the driver
	# label, and this script has no business knowing how that is spelt.
	netif=
	for i in `ifconfig -l 2>/dev/null`
	do
		if [ "$i" != lo0 ]
		then	netif="$i"
			break
		fi
	done
	ifconfig lo0 inet 127.0.0.1 up 2>/dev/null
	if [ -n "$netif" ]
	then
		# Ask the network what this machine is called, and fall back
		# to the addresses QEMU's user-mode networking hands out
		# anyway - the guest is 10.0.2.15, the gateway and the DNS
		# forwarder are .2 and .3.  The fallback is worth keeping
		# even though slirp runs a DHCP server of its own: it is
		# what makes "the lease did not arrive" visible as itself
		# rather than as a machine with no address and no reason.
		ifconfig "$netif" up
		leased=
		if [ -x /sbin/dhcpcd ]
		then
			echo "Asking for an address on $netif"
			if dhcpcd -q -t 20 "$netif"
			then	leased=yes
			fi
		fi
		if [ -z "$leased" ]
		then
			ifconfig "$netif" inet 10.0.2.15 netmask 255.255.255.0 up
			route -q add default 10.0.2.2
			echo "No lease on $netif: static 10.0.2.15, gateway 10.0.2.2"
		else
			echo "Leased on $netif:"
			ifconfig "$netif" | grep 'inet '
		fi

		# The secret behind TCP initial sequence numbers.  Without
		# one the stack numbers its connections from a known start,
		# which is what makes them guessable from off the machine.
		# An attempt, not a requirement: the only entropy source on
		# this port is interrupt timing, a machine this quiet has
		# almost none, and /dev/random is usually still unseeded
		# here.  Waiting for it would hold up every boot.
		isnlen=`sysctl -n net.inet.tcp.isn_secret |
		    awk '{print length/2}'`
		isn=`dd if=/dev/random bs=$isnlen count=1 2>/dev/null |
		    hexdump -v -e '/1 "%02x"'`
		if [ -n "$isn" ]
		then	sysctl -qw net.inet.tcp.isn_secret=$isn
		fi
	else
		echo "WARNING: no network interface"
	fi
fi

exit 0
END_RC
	# The disk root is not the ramdisk.  The ramdisk holds what it takes
	# to reach a root; the disk has room, so it gets everything the tree
	# installed into DESTDIR on top of the ramdisk's device nodes, /etc
	# and boot servers.  evbarm64_rootproto.py merges the two.
	python3 releasetools/evbarm64_rootproto.py \
		"${RAMDISK_OBJ}/proto.gen" "${RAMDISK_OBJ}" "${DESTDIR}" \
		> "${WORK_DIR}/proto.full"
	sed "s|^\([ 	]*rc ---755 0 0 \).*|\1${WORK_DIR}/rc.disk|" \
		"${WORK_DIR}/proto.full" > "${WORK_DIR}/proto.disk"
	# mkfs.mfs sizes a file system to the device it is given, so the
	# device has to exist at its full size first; seeking past the end
	# makes it sparse, so the image costs what is written to it.
	rm -f "${DISK}"
	dd if=/dev/zero of="${DISK}" bs=1M count=0 seek=${DISK_MB} 2>/dev/null
	# The proto names its files by absolute path, so the working
	# directory does not matter.
	# The disk is V4: 64-bit sizes and times, variable-length directory
	# entries.  The ramdisk stays V3 - it is the boot image, and its
	# format is not what this is about.  MKFS_VERSION=-3 makes a V3
	# disk instead, which is how the two are compared.
	(${MKFSMFS} ${MKFS_VERSION:--4} -B 4096 \
		-b $((${DISK_MB} * 1024 * 1024 / 4096)) \
		"${DISK}" "${WORK_DIR}/proto.disk")
	fi
	BOOTARGS=${BOOTARGS:-"rootdevname=c0d0 console=tty00"}
else
	BOOTARGS=${BOOTARGS:-"bootramdisk=1 console=tty00"}
fi

virt=virt
if [ ${el2} -eq 1 ]
then
	virt="${virt},virtualization=on"
fi
if [ ${gicv3} -eq 1 ]
then
	virt="${virt},gic-version=3"
fi

cmd="${QEMU} -M ${virt} -cpu ${QEMU_CPU} -m ${QEMU_MEM} -smp ${QEMU_SMP}"
cmd="${cmd} -display none -serial stdio"
# User-mode networking on the machine's virtio-mmio transport: the guest is
# 10.0.2.15, the gateway 10.0.2.2, the DNS forwarder 10.0.2.3.  Nothing is
# forwarded inward; this is for the system to reach out.  A machine whose
# system has no network driver just sees one more transport it ignores.
# QEMU_HOSTFWD: пробросить TCP-порт хоста внутрь, чтобы входить по ssh
# снаружи - через сетевой драйвер и стек, а не через loopback внутри гостя.
# Проверка ssh через 127.0.0.1 ни драйвера, ни пути пакета через lwip не
# касается вовсе, и однажды это сбило с толку.
#
# Правил можно перечислить несколько через запятую, и у каждого свой порт
# гостя: "2222" - хостовый 2222 на 22 гостя, "2222,2223:2223" - ещё и
# хостовый 2223 на 2223 гостя. Нескольких сразу требует сравнение двух
# слушателей в одной загрузке: один и тот же вопрос, заданный слушателю,
# который уже обслужил соединение, и свежему, - и без второго порта его
# не задать.
if [ -n "${QEMU_HOSTFWD:-}" ]
then	fwd=""
	for rule in `echo "${QEMU_HOSTFWD}" | tr ',' ' '`
	do	case "${rule}" in
		*:*)	hostport=${rule%%:*}; guestport=${rule##*:} ;;
		*)	hostport=${rule}; guestport=22 ;;
		esac
		fwd="${fwd},hostfwd=tcp::${hostport}-:${guestport}"
	done
	cmd="${cmd} -netdev user,id=net0${fwd}"
else	cmd="${cmd} -netdev user,id=net0"
fi
cmd="${cmd} -device virtio-net-device,netdev=net0"
cmd="${cmd} -kernel ${KERNEL_BIN} -initrd ${ARCHIVE}"
if [ ${disk} -eq 1 ]
then
	# A virtio-blk device on the machine's virtio-mmio transport, which is
	# what the device tree describes; the PCI variant would need a PCI
	# server this system does not have.
	# discard=unmap: a discard from the guest punches a hole in the
	# image, the way it frees a block on flash.  Without it QEMU answers
	# the request with success and does nothing, and there would be no
	# way to tell from the host whether the request went through.
	cmd="${cmd} -drive if=none,id=hd0,file=${DISK},format=raw,discard=unmap"
	cmd="${cmd} -device virtio-blk-device,drive=hd0"
fi
cmd="${cmd} -append \"${BOOTARGS}\""
if [ -n "${QEMU_TIMEOUT}" ]
then
	cmd="timeout ${QEMU_TIMEOUT} ${cmd}"
fi

echo ""
echo "RAM image in ${WORK_DIR}:"
ls -l "${KERNEL_BIN}" "${ARCHIVE}"
if [ ${disk} -eq 1 ]
then
	ls -l "${DISK}"
fi
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
