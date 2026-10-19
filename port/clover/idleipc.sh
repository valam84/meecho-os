#!/bin/sh
# idleipc.sh [SEC]: who talks to whom while the desktop idles.  Takes every
# process's row of the kernel's message matrix (/proc/PID/ipc) and its
# times (/proc/PID/psinfo) twice, SEC seconds apart (default 20), with
# kern.cp_time around, and prints the differences:
#
#   cpu NAME PID USER_MS SYS_MS        time used in the window, by process
#   msg FROM(PID) TO COUNT             messages sent in the window
#   kmsg FROM(PID) COUNT               of them, sent by the kernel in its
#                                      name (page faults to VM, mostly)
#   forks N                            processes made in the window
#
# A look reads the files with the shell's own read, and makes no process:
# the looks would otherwise be most of what they measure (the first version
# forked three programs a process, and VM's time was mostly its own).  Run
# it on the tablet with nobody touching it.  Written for
# port/clover/hw-cpu.md, "Фон на ядре 0".
sec=${1:-20}
snap() {
	for d in /proc/[0-9]* /proc/-[0-9]*; do
		p=${d#/proc/}
		[ -r $d/ipc ] || continue
		# psinfo: version type endpoint name state blocked priority
		# user_time sys_time ... (fields 8 and 9, ticks)
		read v ty ep nm st bl pr ut sy rest < $d/psinfo 2>/dev/null ||
		    continue
		echo "T $p $nm $ut $sy"
		while read w a b c n cnt; do
			case $w in
			to)	echo "M $p $n $cnt" ;;
			kernel)	echo "K $p $a" ;;
			esac
		done < $d/ipc
	done
}
cp0=$(/sbin/sysctl -n kern.cp_time)
sleep 1		# the sysctl and its fork out of the window
: & pid0=$!
snap > /tmp/idleipc.0

sleep $sec &
w=$!
wait $w
snap > /tmp/idleipc.1
cp1=$(/sbin/sysctl -n kern.cp_time)
echo "forks $((w - ${pid0:-0}))	(pids from the sleep: approximate)"
cat /tmp/idleipc.0 /tmp/idleipc.1 | awk -v hz=100 '
$1 == "T" { k = $2; if (seen[k]++) { du[k] = $4 - u[k]; ds[k] = $5 - s[k] }
	else { u[k] = $4; s[k] = $5 } nm[k] = $3 }
$1 == "M" { k = $2 " " $3; if (k in m0) dm[k] = $4 - m0[k]; else { m0[k] = $4; dm[k] = -1 } }
$1 == "K" { k = $2; if (k in k0) dk[k] = $3 - k0[k]; else k0[k] = $3 }
END {
	for (k in du) if (du[k] + ds[k] > 0)
		printf "cpu %s %s %d %d\n", nm[k], k, du[k] * 1000 / hz, ds[k] * 1000 / hz
	for (k in dm) if (dm[k] > 0) { split(k, a, " ");
		printf "msg %s(%s) %s %d\n", nm[a[1]], a[1], a[2], dm[k] }
	for (k in dk) if (dk[k] > 0) printf "kmsg %s(%s) %d\n", nm[k], k, dk[k]
}' | sort -k1,1 -k4nr
echo "cp_time0 $cp0"
echo "cp_time1 $cp1"
