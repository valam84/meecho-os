#!/bin/sh
# Сессия с платой по расписанию: открыть мост один раз, писать всё в журнал
# и слать команды по таймингу.
#
# board-run.sh для этого не годится: он рассчитан на уже открытый шелл и
# шлёт команды с одинаковой паузой, а тут надо сначала переждать загрузку,
# потом войти, потом гонять драйвер. Порт держит ровно один процесс, поэтому
# и запись, и ввод должны быть в одном.
#
#   board-session.sh расписание.txt [журнал]
#
# Строки расписания: "<секунд> <что послать>". Пустое "что послать" — просто
# подождать. Строки, начинающиеся с #, пропускаются.
#
#   0   ""
#   55  root
#   5   /sbin/minix-service up /service/sdmmc -dev /dev/c0d0
set -u

COM=${COM:-COM5}
BAUD=${BAUD:-1500000}
PWSH=${PWSH:-/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe}
# Прямые слэши намеренно: обратные не переживают путь через socat EXEC.
PS1_SCRIPT=${PS1_SCRIPT:-D:/minix/port/board-pipe.ps1}
LINK=${LINK:-$HOME/.board-tty-session}

SCHED=${1:-}
LOG=${2:-/tmp/board-session.log}
[ -n "$SCHED" ] && [ -f "$SCHED" ] || { echo "usage: board-session.sh расписание [журнал]" >&2; exit 2; }

rm -f "$LINK" "$LOG"
socat "PTY,link=$LINK,raw,echo=0" \
      "EXEC:\"$PWSH -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $PS1_SCRIPT -Port $COM -Baud $BAUD\",pipes" \
      2>/tmp/board-socat-session.err &
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
[ -e "$LINK" ] || { echo "socat не поднял pty" >&2; tail -5 /tmp/board-socat-session.err >&2; exit 1; }
sleep 1

# Отдельные open на чтение и на запись рвут сессию socat, поэтому один
# дескриптор в обе стороны.
exec 3<>"$LINK"
cat <&3 > "$LOG" &
CAT_PID=$!

echo "мост поднят на $COM, журнал $LOG" >&2

while IFS= read -r line; do
	case "$line" in
		'#'*|'') continue ;;
	esac
	delay=${line%% *}
	cmd=${line#* }
	[ "$cmd" = "$line" ] && cmd=""
	case "$cmd" in
	'WAIT '*)
		# Ждать появления текста в журнале, но не дольше <delay>
		# секунд. Гадать по времени не выходит: загрузка идёт то
		# быстрее, то медленнее, и один сдвиг на пять секунд уводит
		# весь остаток расписания в приглашение login.
		want=${cmd#WAIT }
		waited=0
		while [ "$waited" -lt "$delay" ]; do
			if grep -aqF "$want" "$LOG" 2>/dev/null; then break; fi
			sleep 1
			waited=$((waited + 1))
		done
		if [ "$waited" -ge "$delay" ]; then
			echo "!!! не дождался: $want" >&2
		else
			echo "=== дождался: $want" >&2
		fi
		continue
		;;
	esac

	sleep "$delay"
	case "$cmd" in
	'')
		;;
	'<CR>')
		# Голый возврат каретки: этого ждёт U-Boot после смены
		# скорости консоли, и на приглашение login он безвреден.
		printf '\r' >&3
		;;
	*)
		echo ">>> $cmd" >&2
		printf '%s\r' "$cmd" >&3
		;;
	esac
done < "$SCHED"

sleep 2
kill "$CAT_PID" 2>/dev/null; CAT_PID=""
echo "--- журнал ---" >&2
tr -d '\000' < "$LOG" | sed 's/\x1b\[[0-9;?]*[a-zA-Z]//g'
