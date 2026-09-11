#!/bin/sh
# A/B на одной загрузке и одной флешке: тот же стек, тот же замер, только
# ключ irq= у контроллера. Порядок чередуется (B A A B), чтобы усталость
# носителя или разогрев кэшей не легли на одну сторону.
#
# Сперва содержимое, потом скорость: число, снятое с драйвера, который
# читает не то, ничего не стоит. Каждое чтение берёт СВОЙ участок.
PATH=/bin:/sbin:/usr/bin:/usr/sbin; export PATH
LOG=${LOG:-2}

up() {
	minix-service down usb_storage 2>/dev/null
	minix-service down usb_hub 2>/dev/null
	minix-service down usbd 2>/dev/null
	minix-service up /service/xhci -label usbd -args "instance=0 log=$LOG irq=$1" || { echo "xhci: did not start"; return 1; }
	sleep 2
	minix-service up /service/usb_hub -label usb_hub || { echo "usb_hub: did not start"; return 1; }
	sleep 8
	rm -f /dev/usbdisk /dev/usbdiskp0
	mknod /dev/usbdisk b 17 0; mknod /dev/usbdiskp0 b 17 1
	minix-service up /service/usb_storage -label usb_storage -dev /dev/usbdisk || { echo "usb_storage: did not start"; return 1; }
	sleep 5
}

bench() {
	echo "--- content"
	dd if=/dev/usbdisk of=/tmp/u1 bs=4096 count=256 2>/dev/null
	echo "LBA 0, 1 MiB:    $(md5 /tmp/u1 | awk '{print $NF}')"
	dd if=/dev/usbdisk of=/tmp/u2 bs=512 skip=2048 count=2048 2>/dev/null
	echo "LBA 2048, 1 MiB: $(md5 /tmp/u2 | awk '{print $NF}')"
	rm -f /tmp/u1 /tmp/u2
	echo "--- speed"
	for spec in "65536 128 $2" "65536 512 $3" "1048576 32 $4" "4096 512 $5"; do
		set -- $spec
		echo "bs=$1 count=$2 skip=$3: $(dd if=/dev/usbdisk of=/dev/null bs=$1 count=$2 skip=$3 2>&1 | tail -1)"
	done
}

# skip-смещения в блоках bs, по своему участку на каждый прогон (флешка 7.5 ГБ)
n=0
for irq in 1 0 0 1; do
	n=$((n+1))
	base=$((n * 25000))    # 64k-блоки: ~1.5 ГиБ на прогон, всего в 7.5 ГиБ
	echo "=== run $n: irq=$irq $(cut -d' ' -f1 /proc/uptime)"
	echo "=== run $n: irq=$irq" > /dev/console
	up $irq || continue
	bench $irq $base $((base + 200)) $(( (base + 800) / 16 )) $(( (base + 1600) * 16 ))
	echo "=== run $n done $(cut -d' ' -f1 /proc/uptime)"
done
