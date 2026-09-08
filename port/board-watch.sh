#!/bin/sh
# Смотреть с хаба, что реально приходит с платы, пока на ней идёт прогон.
#
# Плата и хаб - в одной сети (192.168.33.0/24), так что вопрос «вышел ли
# кадр и правильный ли он» решается не счётчиками самой платы, а чужими
# глазами. Счётчики драйвера говорят только то, что драйвер думает.
#
#   board-watch.sh start     поднять tcpdump и фоновый ping на плату
#   board-watch.sh stop      остановить и напечатать, что поймалось
#
# Между ними запускается board-cycle.sh.
set -u


# Своя установка описывается в port/board.conf - его нет в репозитории,
# потому что адрес консольной машины и ключ у каждого свои. Образец рядом:
# board.conf.example.
__d=$(dirname "$0")
[ -f "$__d/board.conf" ] && . "$__d/board.conf"
[ -f "$__d/../board.conf" ] && . "$__d/../board.conf"
HUB=${HUB:?port/board.conf: HUB=root@консольная-машина (см. board.conf.example)}
HUB_PORT=${HUB_PORT:-22}
KEY=${KEY:?port/board.conf: KEY=путь к ssh-ключу (см. board.conf.example)}
BOARD_IP=${BOARD_IP:-192.168.33.35}
IF=${IF:-eth0}

SSH="ssh -o BatchMode=yes -o ConnectTimeout=15 -i $KEY"

case "${1:-}" in
start)
	$SSH "$HUB" -p "$HUB_PORT" "
		pkill -f 'tcpdump -i $IF' >/dev/null 2>&1
		pkill -f 'board-watch-ping' >/dev/null 2>&1
		rm -f /tmp/board.pcap
		setsid tcpdump -i $IF -n -e -s 300 -w /tmp/board.pcap \
		    'host $BOARD_IP or arp' </dev/null >/dev/null 2>&1 &
		setsid sh -c 'exec -a board-watch-ping sh -c \"
			for i in \$(seq 400); do
				ping -c 1 -W 1 $BOARD_IP >/dev/null 2>&1
				sleep 1
			done\"' </dev/null >/dev/null 2>&1 &
		sleep 1
		pgrep -f 'tcpdum[p] -i $IF' >/dev/null && echo watching || echo FAILED
	"
	;;
stop)
	$SSH "$HUB" -p "$HUB_PORT" "
		pkill -f 'tcpdump -i $IF' >/dev/null 2>&1
		pkill -f 'board-watch-ping' >/dev/null 2>&1
		sleep 1
		echo '--- что поймалось:'
		tcpdump -r /tmp/board.pcap -n -e 2>/dev/null | head -60
		echo '--- всего кадров:'
		tcpdump -r /tmp/board.pcap 2>/dev/null | wc -l
	"
	;;
*)
	echo "usage: board-watch.sh start|stop" >&2
	exit 2
	;;
esac
