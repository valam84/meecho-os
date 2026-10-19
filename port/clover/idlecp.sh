#!/bin/sh
# idlecp.sh [SEC] [N]: the machine's busy share while nobody touches it -
# kern.cp_time N times (default 3) over SEC seconds (default 20), and
# nothing else in the window: idleipc.sh says who, but its looks cost VM
# and procfs about as much as what they look for once the desktop is quiet.
# Prints a line a window: ticks user, sys, intr, idle (all processors) and
# busy as a share of the machine and in processors' worth.
sec=${1:-20} n=${2:-3}
ncpu=$(/sbin/sysctl -n hw.ncpu)
i=0
while [ $i -lt $n ]; do
	a=$(/sbin/sysctl -n kern.cp_time | tr -cs '0-9' ' ')
	sleep $sec
	b=$(/sbin/sysctl -n kern.cp_time | tr -cs '0-9' ' ')
	echo $a $b | awk -v n=$ncpu -v sec=$sec '{
		u = $6 - $1 + $7 - $2; s = $8 - $3; t = $9 - $4; id = $10 - $5
		all = u + s + t + id
		printf "user %d sys %d intr %d idle %d busy %.2f%% = %.3f cpus\n",
		    u, s, t, id, 100 * (u + s + t) / all, n * (u + s + t) / all
	}'
	i=$((i + 1))
done
