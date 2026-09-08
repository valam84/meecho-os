#!/bin/sh
# Консоль платы CB2 из WSL.
#
# Плата стоит не здесь: её USB-TTL (CH340) приезжает на Windows-хост по сети
# через USB Network Gate и существует тут только как COM-порт. Пробросить сам
# USB в WSL нельзя — usbipd несовместим с фильтром UNG (fusbhub.sys), а ставить
# ещё и linux-клиент UNG значило бы городить второй проброс поверх первого.
# Поэтому переносится не устройство, а байты: socat поднимает pty и держит на
# другом его конце powershell.exe с board-pipe.ps1, который открывает COM-порт.
# Для picocom это обычный последовательный порт — те же ключи, что на macOS:
#   picocom -b 1500000 --databits 8 --parity n --stopbits 1 --flow n
#
#   board-console.sh                 интерактивно (выход: Ctrl+A Ctrl+X)
#   board-console.sh -l boot.log     то же, с записью всего вывода в файл
#   board-console.sh -t 20           послушать 20 секунд и выйти (для скриптов)
#   board-console.sh -t 20 -c 'ls /' послать команду, послушать, выйти
#   COM=COM4 board-console.sh        если UNG переподключил адаптер на другой порт
#
# Порт держится одним процессом: пока идёт эта консоль, board-console.ps1 на
# Windows тот же COM не откроет, и наоборот.
set -e

COM=${COM:-COM3}
BAUD=${BAUD:-1500000}
PWSH=${PWSH:-/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe}
# Прямые слэши намеренно: обратные не переживают путь через socat EXEC,
# а powershell.exe принимает и такой путь.
PS1_SCRIPT=${PS1_SCRIPT:-D:/minix/port/board-pipe.ps1}
LINK=${LINK:-$HOME/.board-tty}

LOG=""
SECS=""
CMD=""
CMD_SET=0
while [ $# -gt 0 ]; do
	case "$1" in
		-l|--log)     LOG=$2;  shift 2 ;;
		-t|--timeout) SECS=$2; shift 2 ;;
		-c|--command) CMD=$2; CMD_SET=1; shift 2 ;;
		-h|--help)    sed -n '2,20p' "$0"; exit 0 ;;
		*) echo "неизвестный ключ: $1" >&2; exit 2 ;;
	esac
done

[ -x "$PWSH" ] || { echo "нет $PWSH" >&2; exit 1; }
command -v socat   >/dev/null || { echo "нет socat: sudo apt install socat" >&2; exit 1; }
command -v picocom >/dev/null || { echo "нет picocom: sudo apt install picocom" >&2; exit 1; }

rm -f "$LINK"
socat "PTY,link=$LINK,raw,echo=0" \
      "EXEC:\"$PWSH -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $PS1_SCRIPT -Port $COM -Baud $BAUD\",pipes" \
      2>/tmp/board-socat.err &
SOCAT_PID=$!
trap 'kill $SOCAT_PID 2>/dev/null; rm -f "$LINK"' EXIT INT TERM

# powershell.exe стартует около секунды, pty появляется раньше, чем открыт COM.
i=0
while [ ! -e "$LINK" ] && [ $i -lt 100 ]; do sleep 0.1; i=$((i + 1)); done
[ -e "$LINK" ] || {
	echo "socat не поднял pty; хвост /tmp/board-socat.err:" >&2
	tail -5 /tmp/board-socat.err >&2
	exit 1
}
sleep 1.5

if [ -n "$SECS" ]; then
	# Неинтерактивный прогон — то же, чем в QEMU служит QEMU_TIMEOUT.
	# picocom здесь не годится: без настоящего tty на stdin он выходит сразу
	# («read zero bytes from stdin»). Читаем pty напрямую, одним дескриптором:
	# отдельные open на чтение и на запись рвут сессию socat.
	exec 3<>"$LINK"
	# -c '' — это «просто нажать Enter»: плата молчит, пока её не окликнут.
	[ "$CMD_SET" = 1 ] && { printf '%s\r' "$CMD" >&3; sleep 0.3; }
	if [ -n "$LOG" ]; then
		timeout "$SECS" cat <&3 | tee "$LOG"
	else
		timeout "$SECS" cat <&3
	fi
	exec 3>&-
	exit 0
fi

set -- -b "$BAUD" --databits 8 --parity n --stopbits 1 --flow n
[ -n "$LOG" ] && set -- "$@" --logfile "$LOG"
[ "$CMD_SET" = 1 ] && set -- "$@" --initstring "$CMD
"
echo "[$COM @ $BAUD 8N1 через $LINK; выход — Ctrl+A Ctrl+X]"
picocom "$@" "$LINK"
