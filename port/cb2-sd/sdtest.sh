#!/bin/sh
# Проверка второго экземпляра sdmmc (гнездо SD, dw-mshc) в живой MEECHO на
# CB2. Запускается по ssh, вывод - на ту сторону; журнал драйвера идёт на
# консоль платы (хаб).
#
#   sh sdtest.sh up        поднять sdmmc_1 из /tmp/sdmmc с log_level=3
#   sh sdtest.sh read      сверить чтение с дайджестами, снятыми в Armbian
#   sh sdtest.sh speed     скорость чтения и записи (в хвосте за p2)
#   sh sdtest.sh write     записать 32 МиБ случайных в хвост за p2, прочитать
#   sh sdtest.sh down      остановить sdmmc_1
#
# Хвост карты за разделом p2 (с 59040 МиБ) ни одной файловой системе не
# принадлежит; его прежнее содержимое (32 МиБ с 59041) сохранено в Armbian:
# /root/meecho-boot-backup/sdtail-59041-32M.bin.
PATH=/sbin:/usr/sbin:/bin:/usr/bin
D=/dev/c1d0

up() {
	lvl=${1:-3}
	minix-service up /tmp/sdmmc -dev $D -label sdmmc_1 \
	    -args "instance=1 log_level=$lvl"
	echo "up rc=$?"
	sleep 1
	ps ax | grep '[s]dmmc'
}

t() { cut -d' ' -f1 /proc/uptime; }

case "$1" in
up)	up "$2" ;;
down)	minix-service down sdmmc_1; echo "down rc=$?" ;;
read)
	echo "0-16M:   $(dd if=$D bs=1m count=16 2>/dev/null | md5)"
	echo "         ждём 5a3f1bd91de74af364a6721581bfb408"
	echo "59041+32M: $(dd if=$D bs=1m skip=59041 count=32 2>/dev/null | md5)"
	echo "         ждём 94f55245db2ef74e00b2eec1b2b6ceed (или записанное)"
	echo "59100+64M: $(dd if=$D bs=1m skip=59100 count=64 2>/dev/null | md5)"
	echo "         ждём 915e0a55f10b983302d8133c51faf115"
	;;
speed)
	a=$(t); dd if=$D of=/dev/null bs=1m skip=40000 count=64 2>&1 | tail -1
	b=$(t); echo "read 64 MiB: $a -> $b"
	a=$(t); dd if=$D of=/dev/null bs=64k skip=650000 count=1024 2>&1 | tail -1
	b=$(t); echo "read 64 MiB bs=64k: $a -> $b"
	;;
write)
	dd if=/dev/urandom of=/tmp/rnd bs=1m count=32 2>/dev/null
	echo "random: $(md5 < /tmp/rnd)"
	a=$(t); dd if=/tmp/rnd of=$D bs=1m seek=59041 2>&1 | tail -1
	b=$(t); echo "write 32 MiB: $a -> $b"
	echo "back:   $(dd if=$D bs=1m skip=59041 count=32 2>/dev/null | md5)"
	;;
*)	echo "usage: sdtest.sh up|read|speed|write|down" >&2; exit 2 ;;
esac
