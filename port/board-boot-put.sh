#!/bin/sh
# Положить kernel.bin и boot.mba из комплекта mkcard.sh в /boot/meecho платы,
# пока на ней вендорский Linux. Из Git Bash.
#
#   board-boot-put.sh [каталог-комплекта]     по умолчанию port/cb2-card
#
# Сверяет md5 после копирования и ставит файлы через mv: FAT-раздел /boot -
# это и путь назад к Armbian, и полупереписанный boot.mba на нём означает
# загрузку, которая встанет на разборе архива, а не на том, что проверяли.
# Прежде чем трогать /boot, спрашивает, кто отвечает: адрес у платы один на
# обе системы (см. board-cycle.sh).
#
# Резервную копию не делает: место на FAT-разделе кончается за три образа.
# Откат - то, что положил туда человек (kernel.bin.NNNN, boot.mba.NNNN).
set -u
__d=$(dirname "$0")
[ -f "$__d/board.conf" ] && . "$__d/board.conf"
BOARD=${BOARD:-cb2-lan}
BOARD_PORT=${BOARD_PORT:-22}
SSH_BIN=${SSH_BIN:-ssh}
SCP_BIN=${SCP_BIN:-$(dirname "$SSH_BIN")/scp.exe}
[ -x "$SCP_BIN" ] || SCP_BIN=scp
MSYS_NO_PATHCONV=1
export MSYS_NO_PATHCONV

KIT=${1:-$__d/cb2-card}
OPTS="-o BatchMode=yes -o ConnectTimeout=15 -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no -o LogLevel=ERROR"

for f in kernel.bin boot.mba; do
	[ -f "$KIT/meecho/$f" ] || { echo "нет $KIT/meecho/$f - сначала mkcard.sh" >&2; exit 1; }
done

sys=$($SSH_BIN $OPTS -p "$BOARD_PORT" "$BOARD" 'uname -s' 2>/dev/null)
[ "$sys" = Linux ] || { echo "на $BOARD отвечает '$sys', а не Linux" >&2; exit 1; }

k=$(md5sum < "$KIT/meecho/kernel.bin" | cut -d' ' -f1)
b=$(md5sum < "$KIT/meecho/boot.mba" | cut -d' ' -f1)

$SCP_BIN $OPTS -P "$BOARD_PORT" "$KIT/meecho/kernel.bin" "$KIT/meecho/boot.mba" \
	"$BOARD:/tmp/" || exit 1

$SSH_BIN $OPTS -p "$BOARD_PORT" "$BOARD" "set -e
	echo '$k  /tmp/kernel.bin' | md5sum -c --quiet
	echo '$b  /tmp/boot.mba' | md5sum -c --quiet
	cp /tmp/kernel.bin /boot/meecho/kernel.bin.new
	cp /tmp/boot.mba /boot/meecho/boot.mba.new
	sync
	echo '$k  /boot/meecho/kernel.bin.new' | md5sum -c --quiet
	echo '$b  /boot/meecho/boot.mba.new' | md5sum -c --quiet
	mv /boot/meecho/kernel.bin.new /boot/meecho/kernel.bin
	mv /boot/meecho/boot.mba.new /boot/meecho/boot.mba
	sync
	rm -f /tmp/kernel.bin /tmp/boot.mba
	md5sum /boot/meecho/kernel.bin /boot/meecho/boot.mba" || exit 1
echo "на плате: kernel.bin $k, boot.mba $b"
