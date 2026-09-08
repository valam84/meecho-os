#!/bin/sh
# Проверка моста COM->pty без picocom: дёрнуть Enter и посмотреть ответ платы.
# Читать и писать надо одним дескриптором: pty закрывается, когда закрылся
# последний открывший его процесс, и socat при wait-slave считает это концом.
set -e
PWSH=/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe
LINK=/tmp/board-test-tty
rm -f "$LINK"

socat -d -d "PTY,link=$LINK,raw,echo=0" \
      "EXEC:\"$PWSH -NoProfile -NonInteractive -ExecutionPolicy Bypass -File D:/minix/port/board-pipe.ps1 -Port COM3 -Baud 1500000\",pipes" \
      2>/tmp/socat.err &
# ',stderr' выше не ставится: тогда ругань powershell смешается с байтами платы.
SOCAT_PID=$!
trap 'kill $SOCAT_PID 2>/dev/null; rm -f "$LINK"' EXIT

i=0
while [ ! -e "$LINK" ] && [ $i -lt 100 ]; do sleep 0.1; i=$((i + 1)); done
[ -e "$LINK" ] || { echo "pty не появился"; sed -n 1,20p /tmp/socat.err; exit 1; }
echo "pty: $LINK -> $(readlink -f "$LINK")"

sleep 3          # powershell.exe стартует около секунды, COM открывается позже
exec 3<>"$LINK"
printf '\r' >&3
sleep 1
printf '\r' >&3
timeout 5 dd bs=1 count=400 <&3 > /tmp/board-test-out 2>/dev/null || true
exec 3>&-

echo "--- получено ($(wc -c < /tmp/board-test-out) байт) ---"
cat -v /tmp/board-test-out
echo "--- socat stderr ---"
tail -5 /tmp/socat.err
