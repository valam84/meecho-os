#!/bin/sh
# The load under which PM panicked twice on CB2 on 2026-10-12, with stoprace
# on top. Runs on MEECHO itself (over ssh); stoprace and fbpeek in /usr/bin.
#
#   stoprace-soak.sh [seconds]          default 720
#
# Short processes writing to the console (tty mirrors it to the screen),
# opening /dev/fb0 (pauses the screen console and redraws it whole on close:
# a cache flush of the frame per open), file-system work in short processes,
# and stoprace - signals into processes that call PM without end. What the
# machine answers afterwards is the result: still up (uptime), stoprace "ok",
# and nothing in the kernel's message buffer about calls served for stopped
# processes (kmsg, with DEBUG_STOPRACE in the kernel).
#
# port/PORTING-LOG.md, "Остановленный процесс не входит в ядро".
T=${1:-720}
end=$(($(date +%s) + T))
PATH=/sbin:/usr/sbin:/bin:/usr/bin
export PATH

stoprace -t "$T" > /tmp/soak-stoprace.txt 2>&1 &
(while [ "$(date +%s)" -lt $end ]; do
	echo "soak $(date +%T)" > /dev/console
done) &
(while [ "$(date +%s)" -lt $end ]; do
	fbpeek 0 0 8 > /dev/null 2>&1
done) &
(while [ "$(date +%s)" -lt $end ]; do
	ls -lR /etc > /dev/null 2>&1
	cat /etc/rc > /dev/null
done) &
wait
echo "soak: $T s done"
uptime
cat /tmp/soak-stoprace.txt
