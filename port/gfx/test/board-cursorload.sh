#!/bin/sh
#
# On the board, over ssh, with meechowm running: what moving the cursor
# costs, without a hand on the mouse.  ROUNDS injector instances of
# usb_mouse (inject=, 32 reports each, one per tick) move the cursor right
# and back; before and after, kern.cp_time and the CPU time of the
# services on the screen's path (fb, input, the compositor).
#
#   sh board-cursorload.sh [ROUNDS]      default 15
PATH=/bin:/sbin:/usr/bin:/usr/sbin
export PATH

ROUNDS=${1:-15}
R=""
L=""
i=0
while [ $i -lt 32 ]; do
	R="$R${R:+,}0:6:0"
	L="$L${L:+,}0:-6:0"
	i=$((i + 1))
done

svc_times() {
	ps axo pid,time,comm | grep -E "service/fb|service/input|meechowm|foot" |
	    grep -v grep
}

echo "== before"
sysctl kern.cp_time
svc_times
n=0
while [ $n -lt $ROUNDS ]; do
	if [ $((n % 2)) -eq 0 ]; then dir=$R; else dir=$L; fi
	minix-service up /service/usb_mouse -label mload$n \
	    -args "inject=$dir injectwait=1" 2>/dev/null
	sleep 2
	minix-service down mload$n 2>/dev/null
	n=$((n + 1))
done
echo "== after $ROUNDS round(s) of 32 reports"
sysctl kern.cp_time
svc_times
grep -c 'frame' /tmp/mwm.log 2>/dev/null
