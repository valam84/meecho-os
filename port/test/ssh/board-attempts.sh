#!/bin/sh
# По два входа по ssh в каждый из перечисленных портов платы, из Git Bash.
#
#   PATH="/c/Windows/System32/OpenSSH:$PATH" sh board-attempts.sh [порты...]
#
# По умолчанию 22 2222 2223 2224.  Каждый вход САМ говорит, куда он вошёл:
# адрес 192.168.33.36 занимают оба - и MEECHO, и вендорский Armbian, к
# которому плата возвращается по сторожу bootwd.  Признак MEECHO -
# uname -s = MEECHO и /proc/uptime в одно число; у Linux их два.
#
# Ходим через хаб (ProxyJump), потому что проброс на маршрутизаторе смотрит
# на .35, а плата получает .36 - см. ~/.ssh/config.
set -u


# Своя установка описывается в port/board.conf - его нет в репозитории,
# потому что адрес консольной машины и ключ у каждого свои. Образец рядом:
# board.conf.example.
__d=$(dirname "$0")
[ -f "$__d/board.conf" ] && . "$__d/board.conf"
[ -f "$__d/../board.conf" ] && . "$__d/../board.conf"
KEY=${KEY:?port/board.conf: KEY=путь к ssh-ключу (см. board.conf.example)}
HUB=${HUB:?port/board.conf: HUB=root@консольная-машина (см. board.conf.example)}
HUB_PORT=${HUB_PORT:-22}
BOARD_IP=${BOARD_IP:-192.168.33.36}
PORTS=${*:-"22 2222 2223 2224"}
OUT=${OUT:-./ssh-attempts.log}

: > "$OUT"

for p in $PORTS; do
	for n in 1 2; do
		echo "=== порт $p, попытка $n ($(date +%H:%M:%S)) ===" | tee -a "$OUT"
		timeout 40 ssh -i "$KEY" -p "$p" \
			-o IdentitiesOnly=yes -o BatchMode=yes \
			-o StrictHostKeyChecking=no \
			-o UserKnownHostsFile=/dev/null \
			-o ConnectTimeout=15 \
			-o "ProxyCommand=ssh -i $KEY -p $HUB_PORT -o BatchMode=yes -o StrictHostKeyChecking=accept-new -W %h:%p $HUB" \
			-v "root@$BOARD_IP" \
			'uname -s; cat /proc/uptime; sysenv rootdevname; echo ATTEMPT-OK' \
			>> "$OUT" 2>&1
		echo "rc=$?" | tee -a "$OUT"
		grep -E 'ATTEMPT-OK|MEECHO|banner|timed out|Connection|Server host key' \
			"$OUT" | tail -4
	done
done

echo
echo "полный журнал: $OUT"
