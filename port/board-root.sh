#!/bin/sh
# Развернуть корень MEECHO на eMMC платы, из Git Bash на Windows.
#
# Образ делает mkroot.sh (там же обоснование, почему образом, а не
# копированием); сюда он приезжает в gzip - гигабайт нулей и десять мегабайт
# файлов ужимаются до пяти, - и разворачивается на /dev/mmcblk1 штатным dd
# вендорского Linux, который на плате пока живёт с SD-карты.
#
# Это не обход драйвера: драйвер проверяется не тем, кто записал том, а тем,
# что система с него потом живёт.
#
# ВНИМАНИЕ: затирает первый гигабайт eMMC без вопросов.
#
#   board-root.sh [образ.gz]
set -u


# Своя установка описывается в port/board.conf - его нет в репозитории,
# потому что адрес консольной машины и ключ у каждого свои. Образец рядом:
# board.conf.example.
__d=$(dirname "$0")
[ -f "$__d/board.conf" ] && . "$__d/board.conf"
[ -f "$__d/../board.conf" ] && . "$__d/../board.conf"
BOARD=${BOARD:?port/board.conf: BOARD=root@плата (см. board.conf.example)}
BOARD_PORT=${BOARD_PORT:-22}
KEY=${KEY:?port/board.conf: KEY=путь к ssh-ключу (см. board.conf.example)}
DEV=${DEV:-/dev/mmcblk1}
IMG=${1:-/d/minix/port/cb2-card/root-emmc.img.gz}

SSH="ssh -o BatchMode=yes -o ConnectTimeout=15 -i $KEY"
SCP="scp -o BatchMode=yes -o ConnectTimeout=15 -i $KEY"

[ -f "$IMG" ] || { echo "нет $IMG - сначала mkroot.sh" >&2; exit 2; }

$SCP -P "$BOARD_PORT" "$IMG" "$BOARD:/tmp/root-emmc.img.gz" || exit 1

# gzip -t до записи: половина образа, доехавшая по ssh, выглядит как образ и
# разворачивается ровно до того места, где кончилась.
$SSH "$BOARD" -p "$BOARD_PORT" "set -e
	gzip -t /tmp/root-emmc.img.gz
	gzip -dc /tmp/root-emmc.img.gz | dd of=$DEV bs=1M conv=fsync 2>&1 | tail -1
	sync
	dd if=$DEV bs=1 skip=1024 count=4 2>/dev/null | od -c | head -1" || exit 1

echo "готово: на $DEV лежит корень; загрузка - board-cycle.sh -e"
