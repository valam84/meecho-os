#!/bin/sh
# N подъёмов стека подряд на одной загрузке; после каждого - дайджесты двух
# участков. Метки идут и в консоль, чтобы журнал драйвера можно было
# разложить по циклам.
PATH=/bin:/sbin:/usr/bin:/usr/sbin; export PATH
N=${1:-10}; ARGS=${2:-"instance=0 log=3"}
i=1
while [ $i -le $N ]; do
	echo "=== cycle $i ($ARGS)"; echo "=== cycle $i ($ARGS)" > /dev/console
	t0=$(cut -d' ' -f1 /proc/uptime)
	minix-service down usb_storage 2>/dev/null
	minix-service down usb_hub 2>/dev/null
	minix-service down usbd 2>/dev/null
	minix-service up /service/xhci -label usbd -args "$ARGS" || echo "xhci: did not start"
	sleep 2
	minix-service up /service/usb_hub -label usb_hub || echo "usb_hub: did not start"
	sleep 10
	rm -f /dev/usbdisk /dev/usbdiskp0
	mknod /dev/usbdisk b 17 0; mknod /dev/usbdiskp0 b 17 1
	minix-service up /service/usb_storage -label usb_storage -dev /dev/usbdisk || echo "usb_storage: did not start"
	sleep 8
	t1=$(cut -d' ' -f1 /proc/uptime)
	ps ax | grep -v grep | grep -E '/service/(xhci|usb_hub|usb_storage)' | awk '{print "  alive:", $NF}'
	rm -f /tmp/u1 /tmp/u2
	dd if=/dev/usbdisk of=/tmp/u1 bs=4096 count=256 2>/tmp/dd1 || cat /tmp/dd1
	dd if=/dev/usbdisk of=/tmp/u2 bs=512 skip=2048 count=2048 2>/tmp/dd2 || cat /tmp/dd2
	echo "  LBA0: $(md5 -q /tmp/u1 2>/dev/null || md5 /tmp/u1 2>/dev/null | awk '{print $NF}')"
	echo "  LBA2048: $(md5 -q /tmp/u2 2>/dev/null || md5 /tmp/u2 2>/dev/null | awk '{print $NF}')"
	rm -f /tmp/u1 /tmp/u2
	echo "  up took $t0 -> $t1"
	echo "=== end $i" > /dev/console
	i=$((i+1))
done
