#!/bin/sh
# Живёт на хабе (/root/hub-watch.sh). Смотрит, что реально приходит с платы.
#
# Отдельным файлом, по той же причине, что и run-sched.sh: строка, переданная
# через ssh, обрастает кавычками быстрее, чем читается, а отказ в ней виден
# только тем, что ничего не произошло.
#
# Свои процессы останавливаются по pid-файлу, а не по маске: на хабе работает
# чужой мониторинг с похожей командной строкой, и pkill -f по слову tcpdump
# гасит его заодно.
#
#   hub-watch.sh start <ip> [интерфейс]
#   hub-watch.sh stop
set -u

PCAP=/tmp/board.pcap
PIDF=/tmp/board-watch.pids

case "${1:-}" in
start)
	IP=${2:-192.168.33.35}
	IF=${3:-eth0}

	[ -f "$PIDF" ] && sh "$0" stop >/dev/null 2>&1
	rm -f "$PCAP" "$PIDF"

	setsid tcpdump -i "$IF" -n -e -s 300 -w "$PCAP" \
	    "host $IP or arp" </dev/null >/tmp/board-watch.err 2>&1 &
	echo $! >> "$PIDF"

	setsid sh -c "while :; do ping -c 1 -W 1 $IP >/dev/null 2>&1; \
	    sleep 1; done" </dev/null >/dev/null 2>&1 &
	echo $! >> "$PIDF"

	sleep 2
	if [ -s "$PCAP" ] || kill -0 "$(head -1 $PIDF)" 2>/dev/null; then
		echo "watching $IP on $IF"
	else
		echo "FAILED"
		cat /tmp/board-watch.err
	fi
	;;
stop)
	if [ -f "$PIDF" ]; then
		while read pid; do
			kill "$pid" 2>/dev/null
			pkill -P "$pid" 2>/dev/null
		done < "$PIDF"
		rm -f "$PIDF"
	fi
	sleep 1
	echo "--- кадры на проводе:"
	tcpdump -r "$PCAP" -n -e 2>/dev/null | head -80
	echo "--- всего:"
	tcpdump -r "$PCAP" 2>/dev/null | wc -l
	;;
*)
	echo "usage: hub-watch.sh start <ip> [if] | stop" >&2
	exit 2
	;;
esac
