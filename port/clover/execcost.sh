#!/bin/sh
# execcost.sh N COMMAND...: what running COMMAND N times costs VM - its
# kernel calls (VM's row of the message matrix, to "system") and its time,
# less the same N seconds idle.  For port/clover/hw-cpu.md, "Фон на ядре 0":
# which of the desktop's periodic programs make VM busy.
n=$1; shift
look() {	# VM's calls to the kernel, VM's time in ticks
	c=0
	while read w a b c2 name cnt; do
		[ "$w" = to ] && [ "$name" = system ] && c=$cnt
	done < /proc/11/ipc
	read v ty ep nm st bl pr ut sy rest < /proc/11/psinfo
	echo "$c $ut"
}
set -- "$@"
a=$(look)
i=0
while [ $i -lt $n ]; do
	"$@" < /dev/null > /dev/null 2>&1
	i=$((i + 1))
done
b=$(look)
echo "$a $b" | while read c0 t0 c1 t1; do
	echo "runs $n: vm calls $((c1 - c0)) ($(( (c1 - c0) / n )) a run)," \
	    "vm time $(( (t1 - t0) * 10 )) ms ($(( (t1 - t0) * 10 / n )) ms a run)"
done
