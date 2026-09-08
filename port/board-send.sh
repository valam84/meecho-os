#!/bin/sh
# Отправить файл на плату по YMODEM через ту же консоль.
#
# Плата стоит в другом месте и приезжает сюда только последовательным портом,
# поэтому доставить на неё kernel.bin или boot.mba по TFTP нельзя: наш TFTP для
# неё недостижим. Зато U-Boot умеет `loady`, а 1500000 бод — это ~150 КБ/с, то
# есть образ ядра уезжает за десятки секунд.
#
# На плате (в U-Boot):   loady 0x40200000
# Здесь:                 board-send.sh ~/obj-evbarm64/work/kernel.bin
#
# Скрипт сам консоль не занимает: пока идёт передача, другой терминал на том же
# COM держать нельзя — порт открывается ровно одним процессом.
set -e

FILE=$1
[ -n "$FILE" ] || { echo "usage: board-send.sh <файл> [ещё файлы...]" >&2; exit 2; }
for f in "$@"; do
	[ -f "$f" ] || { echo "нет файла: $f" >&2; exit 1; }
done

COM=${COM:-COM3}
BAUD=${BAUD:-1500000}
PWSH=${PWSH:-/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe}
PS1_SCRIPT=${PS1_SCRIPT:-D:/minix/port/board-pipe.ps1}
LINK=${LINK:-$HOME/.board-tty-send}

command -v sx >/dev/null || { echo "нет sx: sudo apt install lrzsz" >&2; exit 1; }

rm -f "$LINK"
socat "PTY,link=$LINK,raw,echo=0" \
      "EXEC:\"$PWSH -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $PS1_SCRIPT -Port $COM -Baud $BAUD\",pipes" \
      2>/tmp/board-socat-send.err &
SOCAT_PID=$!
trap 'kill $SOCAT_PID 2>/dev/null; rm -f "$LINK"' EXIT INT TERM

i=0
while [ ! -e "$LINK" ] && [ $i -lt 100 ]; do sleep 0.1; i=$((i + 1)); done
[ -e "$LINK" ] || { echo "socat не поднял pty" >&2; tail -5 /tmp/board-socat-send.err >&2; exit 1; }
sleep 1.5

echo "[$COM @ $BAUD, YMODEM: $*]"
echo "[на плате должен быть запущен приёмник: loady <адрес> в U-Boot или rx в Linux]"
# --ymodem, потому что loady у U-Boot — именно YMODEM; -vv показывает ход передачи.
sx --ymodem -vv "$@" < "$LINK" > "$LINK"
