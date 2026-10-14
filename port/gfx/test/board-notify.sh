#!/bin/sh
#
# On the board, over ssh: meechowm-notify on the desktop board-desk.sh
# left running - what a look at /proc/service costs here, and a service
# restarted for the card to show (stage 11.7, qemu-notify.py on QEMU).
# The screen afterwards: board-shot.sh, from the host.
#
#   sh board-notify.sh cost          -m 200: milliseconds and messages a
#                                    look, the notifier's own and in all
#   sh board-notify.sh refresh SVC   minix-service refresh SVC, and where
#                                    the card is (meechowm -m state)
#   sh board-notify.sh lwip          refresh lwip from a detached shell,
#                                    and the network set up again after it:
#                                    lwip's sockets go with it, this ssh
#                                    session, sshd's listening socket and
#                                    the lease among them.  Log:
#                                    /tmp/notify-lwip.log
PATH=/bin:/sbin:/usr/bin:/usr/sbin
XDG_RUNTIME_DIR=/tmp/xdg-0
export PATH XDG_RUNTIME_DIR

where() {
	n=0
	while [ $n -lt 15 ] && ! meechowm -m state | grep -q '^layer meechowm-notify'
	do
		sleep 1
		n=$((n + 1))
	done
	meechowm -m state | grep '^layer'
}

case "$1" in
cost)
	meechowm-notify -m 200
	;;
refresh)
	cat /proc/service/$2 | grep -E 'endpoint|restarts'
	minix-service refresh $2
	echo "refresh $2: $?"
	cat /proc/service/$2 | grep -E 'endpoint|restarts'
	where
	;;
lwip)
	trap '' HUP
	(
		sleep 2
		minix-service refresh lwip
		echo "refresh lwip: $?"
		grep -E 'endpoint|restarts' /proc/service/lwip
		sleep 3
		netif=
		for i in `ifconfig -l`; do
			[ "$i" = lo0 ] || { netif=$i; break; }
		done
		ifconfig lo0 inet 127.0.0.1 up
		ifconfig "$netif" up
		for f in /var/run/dhcpcd*.pid; do
			[ -f "$f" ] && kill `cat "$f"`
		done
		sleep 1
		dhcpcd -t 20 "$netif"
		echo "dhcpcd: $?"
		ifconfig "$netif" | grep 'inet '
		[ -f /var/run/sshd.pid ] && kill `cat /var/run/sshd.pid`
		sleep 1
		/usr/sbin/sshd
		echo "sshd: $?"
		where
	) > /tmp/notify-lwip.log 2>&1 < /dev/null &
	echo "lwip goes in 2 s; this session with it"
	;;
*)
	echo "usage: sh board-notify.sh cost | refresh SERVICE | lwip" >&2
	exit 2
	;;
esac
