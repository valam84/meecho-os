#!/bin/sh
# The battery's current on the Mi Pad 4 under a load - what the governors
# of qcom_cpufreq and adreno save (port/clover/hw-cpu.md, the governors'
# section).  Run on the tablet as root, from ssh:
#
#	sh powerbench.sh TAG [SECONDS [LOAD]]
#
#   LOAD   idle (the default): nothing started, the desktop as it is
#          cube: glcube offscreen, 512x512 - one core busy, the GPU not
#          cube16: the same at 1600x1600 - the GPU busy too
#          cpu:  one awk loop - one core busy, the GPU idle
#          cpu8: eight - every core busy
#
# Every two seconds sysctl minix.pmic.battery (qcom_pmic reads the fuel
# gauge then): charge %, mV, mA - + is into the battery - and the charger's
# state.  On the hub's cable the tablet charges: what the battery gets is
# what the charger gives less what the system takes, so a system that takes
# less shows as less discharge or more charge - comparable between runs only
# in the same charger state, which the summary says.  Around the run
# kern.cp_time, and after it the governors' last report lines from kmsg.
# The summary goes to stdout and /tmp/power-TAG.txt.
PATH=/bin:/sbin:/usr/bin:/usr/sbin
tag=${1:?TAG}
secs=${2:-120}
load=${3:-idle}
o=/tmp/power-$tag.txt
s=/tmp/power-$tag.samples

pids=
case "$load" in
idle)	;;
cube)	/usr/tests/gles/glcube -o -q -n 100000000 > /dev/null 2>&1 &
	pids=$! ;;
cube16)	/usr/tests/gles/glcube -o -q -s 1600 -n 100000000 > /dev/null 2>&1 &
	pids=$! ;;
cpu)	awk 'BEGIN { for (;;) x++ }' & pids=$! ;;
cpu8)	for i in 1 2 3 4 5 6 7 8; do
		awk 'BEGIN { for (;;) x++ }' & pids="$pids $!"
	done ;;
*)	echo "powerbench: LOAD is idle, cube, cube16, cpu or cpu8" >&2; exit 2 ;;
esac

{
	echo "== $tag: $load, $secs s; `sysenv cpugov 2>/dev/null` `sysenv gpugov 2>/dev/null` `sysenv gpusleep 2>/dev/null`"
	echo "cp_time before: `sysctl -n kern.cp_time`"
} > $o
: > $s
sleep 4					# the load under way first
n=0
while [ $n -lt $((secs / 2)) ]; do
	sysctl -n minix.pmic.battery >> $s
	sleep 2
	n=$((n + 1))
done
echo "cp_time after:  `sysctl -n kern.cp_time`" >> $o
for p in $pids; do
	kill $p 2> /dev/null
done

awk -v tag="$tag" '
	{ n++; ma = $3; sum += ma; if (n == 1 || ma < lo) lo = ma
	  if (n == 1 || ma > hi) hi = ma; st[$4]++; mv = $2; soc = $1 }
	END {
		states = ""
		for (k in st) states = states " " k "=" st[k]
		if (n == 0) { print tag ": no samples"; exit }
		printf "%s: %d samples, current %.1f mA (min %d, max %d); " \
		    "%d%%, %d mV;%s\n", tag, n, sum / n, lo, hi, soc, mv, states
	}' $s >> $o
kmsg | grep -a -E "qcom_cpufreq.*switches|adreno.*switches" | tail -3 >> $o
cat $o
