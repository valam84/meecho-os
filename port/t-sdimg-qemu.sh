#!/bin/sh
# Образ SD-карты CB2 (mksdimg.sh) - проверка без платы.
#
# U-Boot и FAT в QEMU не исполнить, зато всё, что после загрузчика, -
# можно: образ подставляется диском virtio, и MEECHO грузится с корнем на
# его втором разделе (c0d0p1 - на плате это c1d0p1, тот же код разбора MBR
# и тот же корень, другой только экземпляр драйвера). Диск - snapshot=on:
# образ, который пойдёт на карту, не меняется.
#
#   t-sdimg-qemu.sh [образ]
set -eu
WORK=${WORK:-$HOME/obj-evbarm64/work}
IMG=${1:-$WORK/meecho-cb2-sd.img}
SECTORS=$(($(stat -c %s "$IMG") / 512))

# Корень - то, что этот образ несёт; host fsck4 смотрит его до загрузки.
start=$(sfdisk -d "$IMG" | awk '/type=81/ { gsub(",", "", $4); print $4 }')
echo "root partition at sector $start"
dd if="$IMG" of="$WORK/sdimg-root.img" bs=512 skip="$start" status=none
"${FSCK4:-$HOME/fsck4-host}" -n "$WORK/sdimg-root.img" | tail -3
rm -f "$WORK/sdimg-root.img"

{
	sleep 75; echo root; sleep 5
	echo 'mount; df; ls /service | wc -l; uname -a'; sleep 4
	echo 'printf "s%s\n" ok'; sleep 2
	echo /sbin/poweroff; sleep 8
} | timeout ${QEMU_TIMEOUT:-160} qemu-system-aarch64 -M virt -cpu cortex-a72 \
	-m 1024 -smp 2 -display none -serial stdio \
	-netdev user,id=net0 -device virtio-net-device,netdev=net0 \
	-device virtio-rng-device \
	-drive if=none,id=sd,format=raw,file="$IMG",snapshot=on \
	-device virtio-blk-device,drive=sd \
	-kernel "$WORK/kernel.bin" -initrd "$WORK/boot.mba" \
	-append "rootdevname=c0d0p1 console=tty00" 2>&1 | tr -d '\r'
