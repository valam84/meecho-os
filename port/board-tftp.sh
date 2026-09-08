#!/bin/sh
# Опубликовать сборку MEECHO на TFTP-сервере хаба.
#
# Загрузка по сети - веха 8.1. Она НЕ работает на этой плате, и не по нашей
# вине: вендорский U-Boot (Armbian для RK3566) собран без CONFIG_NET вовсе.
# Проверено списком его команд: из девяноста восьми нет ни tftpboot, ни ping,
# ни bootp, ни nfs, а `dhcp` отвечает "Unknown command". Разбор и цифры -
# PORTING-LOG.md, «Этап 8.1».
#
# Скрипт оставлен рабочим, потому что серверная половина проверена целиком:
# tftpd-hpa на хабе отдаёт файлы, плата их забирает (из Linux, curl), а
# загрузочный скрипт умеет и попытку по сети, и откат на карту - откат
# проверен на живой плате. Не хватает ровно одного: загрузчика с сетью.
#
#   board-tftp.sh [каталог-комплекта]
#
# После этого положить на плату файл /boot/meecho/net - и загрузка попробует
# сеть. Без файла попытки нет: иначе каждая загрузка печатала бы
# "Unknown command 'dhcp'".
set -eu

KIT=${1:-/d/minix/port/cb2-card}

# Своя установка описывается в port/board.conf - его нет в репозитории,
# потому что адрес консольной машины и ключ у каждого свои. Образец рядом:
# board.conf.example.
__d=$(dirname "$0")
[ -f "$__d/board.conf" ] && . "$__d/board.conf"
[ -f "$__d/../board.conf" ] && . "$__d/../board.conf"
HUB=${HUB:?port/board.conf: HUB=root@консольная-машина (см. board.conf.example)}
HUB_PORT=${HUB_PORT:-22}
KEY=${KEY:?port/board.conf: KEY=путь к ssh-ключу (см. board.conf.example)}
TFTPDIR=${TFTPDIR:-/srv/tftp/meecho}

SSH="ssh -o BatchMode=yes -o ConnectTimeout=15 -i $KEY"
SCP="scp -o BatchMode=yes -o ConnectTimeout=15 -i $KEY"

for f in kernel.bin boot.mba; do
	[ -f "$KIT/meecho/$f" ] || {
		echo "нет $KIT/meecho/$f - сначала mkcard.sh" >&2
		exit 2
	}
done

$SSH "$HUB" -p "$HUB_PORT" "mkdir -p $TFTPDIR"
$SCP -P "$HUB_PORT" "$KIT/meecho/kernel.bin" "$KIT/meecho/boot.mba" \
	"$HUB:$TFTPDIR/"
$SSH "$HUB" -p "$HUB_PORT" "chmod 644 $TFTPDIR/*; ls -l $TFTPDIR; md5sum $TFTPDIR/*"

echo "готово: файлы на tftp://192.168.33.2/meecho/"
