#!/bin/sh
# Выполнить команды в уже открытой сессии на плате и забрать весь вывод.
#
# Вход выполняет человек: пароли сюда не вводятся. Сессия на плате остаётся
# залогиненной после выхода из терминала, поэтому подключение подхватывает
# готовый шелл.
#
#   board-run.sh 'id' 'cat /boot/armbianEnv.txt'
#   PAUSE=3 board-run.sh 'dmesg | tail -40'
#
# Сырой поток остаётся в /tmp/board-run.raw.
set -e

COM=${COM:-COM3}
BAUD=${BAUD:-1500000}
PWSH=${PWSH:-/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe}
PS1_SCRIPT=${PS1_SCRIPT:-D:/minix/port/board-pipe.ps1}
LINK=${LINK:-$HOME/.board-tty-run}
RAW=${RAW:-/tmp/board-run.raw}
PAUSE=${PAUSE:-2}

[ $# -gt 0 ] || { echo "usage: board-run.sh 'команда' ['команда'...]" >&2; exit 2; }

rm -f "$LINK" "$RAW"
socat "PTY,link=$LINK,raw,echo=0" \
      "EXEC:\"$PWSH -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $PS1_SCRIPT -Port $COM -Baud $BAUD\",pipes" \
      2>/tmp/board-socat-run.err &
SOCAT_PID=$!
CAT_PID=""
cleanup() {
	[ -n "$CAT_PID" ] && kill "$CAT_PID" 2>/dev/null
	kill "$SOCAT_PID" 2>/dev/null
	rm -f "$LINK"
}
trap cleanup EXIT INT TERM

i=0
while [ ! -e "$LINK" ] && [ $i -lt 100 ]; do sleep 0.1; i=$((i + 1)); done
[ -e "$LINK" ] || { echo "socat не поднял pty" >&2; tail -5 /tmp/board-socat-run.err >&2; exit 1; }
sleep 1.5

exec 3<>"$LINK"
cat <&3 > "$RAW" &
CAT_PID=$!

# Пустой Enter: убедиться, что на том конце приглашение, а не приглашение login.
printf '\r' >&3
sleep 1

for c in "$@"; do
	printf '%s\r' "$c" >&3
	sleep "$PAUSE"
done

sleep 1
kill "$CAT_PID" 2>/dev/null; CAT_PID=""
cat "$RAW"
