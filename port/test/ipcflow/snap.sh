#!/bin/sh
# snap.sh PID TAG [ROWPID]: once PID has stopped sending - its row and its
# /proc/ktrace line the same in two looks a second apart - print the row of
# ROWPID (default PID) from /proc/ROWPID/ipc and the "proc" rows of
# /proc/ktrace, between markers the stand (qemu-ipcflow.py) parses.
#
# Stability, not a stopped state: MINIX's PM ends a process on SIGSTOP
# instead of stopping it, so the probe waits in open(2) of a FIFO, and the
# second look is what tells "waiting there" from "between the line it
# printed and that open".  Fetched by the guest with the probe; a shell
# function would not survive the tty's line length.
p=$1; t=$2; r=${3:-$1}
look() {
	s=$(sed -n 's/^self \([0-9]*\) .*/\1/p' /proc/$p/ipc)
	sed '/^tsc /d' /proc/$p/ipc
	grep "^proc $s " /proc/ktrace
}
prev=$(look); n=0
while :; do
	sleep 1
	cur=$(look)
	[ -n "$cur" ] && [ "$cur" = "$prev" ] && break
	prev=$cur
	n=$((n + 1))
	[ $n -gt 60 ] && { echo "SNAP $t: $p never settled"; exit 1; }
done
printf 'SNAP%s %s\n' -BEGIN "$t"
sed 's/^/IPC /' /proc/$r/ipc
grep '^proc ' /proc/ktrace | sed 's/^/KT /'
printf 'SNAP%s %s\n' -END "$t"
