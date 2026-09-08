#!/bin/sh
# Поймать U-Boot на перезагрузке и снять с него обстановку.
#
# Плата удалённая, консоль — единственный канал, а окно autoboot у Rockchip
# U-Boot короткое (обычно секунда). Поэтому скрипт непрерывно шлёт пробелы,
# пока в потоке не появится приглашение, затем спам прекращает и выполняет
# команды. Питание/reset в это время передёргивает человек у платы.
#
#   board-uboot.sh                       ждать 90 с, снять стандартный набор
#   board-uboot.sh -w 180                ждать дольше
#   board-uboot.sh -w 90 'printenv' 'mmc list'   свои команды
#
# Весь сырой поток остаётся в /tmp/board-uboot.raw — там же виден лог BL31 и
# ранняя загрузка, если приглашение поймать не удалось.
set -e

COM=${COM:-COM3}
BAUD=${BAUD:-1500000}
PWSH=${PWSH:-/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe}
PS1_SCRIPT=${PS1_SCRIPT:-D:/minix/port/board-pipe.ps1}
LINK=${LINK:-$HOME/.board-tty-uboot}
RAW=${RAW:-/tmp/board-uboot.raw}

WAIT=90
if [ "$1" = "-w" ]; then WAIT=$2; shift 2; fi
if [ $# -gt 0 ]; then CMDS="$*"; else
	# Что нужно знать, чтобы запустить своё ядро: версия и сборка, карта памяти
	# и адреса загрузки, откуда грузится, есть ли loady (единственный наш канал
	# доставки образа) и есть ли сеть.
	CMDS="version|bdinfo|printenv|mmc list|help"
fi

rm -f "$LINK" "$RAW"
socat "PTY,link=$LINK,raw,echo=0" \
      "EXEC:\"$PWSH -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $PS1_SCRIPT -Port $COM -Baud $BAUD\",pipes" \
      2>/tmp/board-socat-uboot.err &
SOCAT_PID=$!
CAT_PID=""
SPAM_PID=""
cleanup() {
	[ -n "$SPAM_PID" ] && kill "$SPAM_PID" 2>/dev/null
	[ -n "$CAT_PID" ]  && kill "$CAT_PID"  2>/dev/null
	kill "$SOCAT_PID" 2>/dev/null
	rm -f "$LINK"
}
trap cleanup EXIT INT TERM

i=0
while [ ! -e "$LINK" ] && [ $i -lt 100 ]; do sleep 0.1; i=$((i + 1)); done
[ -e "$LINK" ] || { echo "socat не поднял pty" >&2; tail -5 /tmp/board-socat-uboot.err >&2; exit 1; }
sleep 1.5

exec 3<>"$LINK"
cat <&3 > "$RAW" &
CAT_PID=$!

echo "[$COM @ $BAUD; жду перезагрузку платы до $WAIT с — передёрни питание или нажми reset]"

# Пробел прерывает autoboot и безвреден в приглашении: получается пустая команда.
( while :; do printf ' ' >&3; sleep 0.05; done ) &
SPAM_PID=$!

CAUGHT=0
i=0
while [ $i -lt $((WAIT * 4)) ]; do
	# Приглашения, которые встречаются у Rockchip/mainline U-Boot.
	if grep -qE '=> |U-Boot> |Hit any key to stop autoboot' "$RAW" 2>/dev/null; then
		CAUGHT=1
		break
	fi
	sleep 0.25
	i=$((i + 1))
done

kill "$SPAM_PID" 2>/dev/null
SPAM_PID=""
sleep 1

if [ "$CAUGHT" = 0 ]; then
	echo "[приглашение U-Boot не поймано за $WAIT с; сырой поток — $RAW]"
	sleep 1
	kill "$CAT_PID" 2>/dev/null; CAT_PID=""
	echo "--- получено $(wc -c < "$RAW") байт ---"
	cat -v "$RAW" | tail -60
	exit 1
fi

echo "[U-Boot пойман; выполняю команды]"
printf '\r' >&3
sleep 0.5
OLD_IFS=$IFS
IFS='|'
for c in $CMDS; do
	IFS=$OLD_IFS
	printf '%s\r' "$c" >&3
	# printenv и help печатают много, а 1500000 бод не значит, что U-Boot
	# отвечает мгновенно.
	sleep 2
	IFS='|'
done
IFS=$OLD_IFS

sleep 1
kill "$CAT_PID" 2>/dev/null; CAT_PID=""
echo "--- получено $(wc -c < "$RAW") байт, сырой поток в $RAW ---"
cat -v "$RAW"
