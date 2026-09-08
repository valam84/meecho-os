#!/bin/sh
# 8.0.3: тот же замер, что на плате, но на QEMU — чтобы проверить измерялку
# там, где параллелизм заведомо есть (этап 6: четыре цикла стоили 1.78
# времени одного).
#
# Сравниваются две одинаковые работы: четыре счётных цикла подряд в одном
# процессе и четыре одновременно.  Соотношение между ними и есть ответ; оно
# не зависит ни от частоты, ни от того, эмулятор это или железо.
#
#	t-smp-qemu.sh [ядер]
PORT=$(cd "$(dirname "$0")" && pwd)
SRCDIR=${SRCDIR:-$(cd "$PORT/.." && pwd)}

SMP=${1:-4}
N=${N:-6000}
{
	sleep 20; echo root
	sleep 3;  echo "cat /proc/cpuinfo | grep -c processor"
	sleep 3;  echo "t0=\$(awk '{print \$1}' /proc/uptime); j=0; while [ \$j -lt 4 ]; do n=0; while [ \$n -lt $N ]; do n=\$((n+1)); done; j=\$((j+1)); done; t1=\$(awk '{print \$1}' /proc/uptime); awk -v a=\$t0 -v b=\$t1 'BEGIN{printf \"SERIAL4 %.2f\n\", b-a}'"
	sleep 60; echo "t0=\$(awk '{print \$1}' /proc/uptime); j=0; while [ \$j -lt 4 ]; do (n=0; while [ \$n -lt $N ]; do n=\$((n+1)); done) & j=\$((j+1)); done; wait; t1=\$(awk '{print \$1}' /proc/uptime); awk -v a=\$t0 -v b=\$t1 'BEGIN{printf \"PAR4 %.2f\n\", b-a}'"
	sleep 45; echo "t0=\$(awk '{print \$1}' /proc/uptime); (n=0; while [ \$n -lt $N ]; do n=\$((n+1)); done); t1=\$(awk '{print \$1}' /proc/uptime); awk -v a=\$t0 -v b=\$t1 'BEGIN{printf \"PAR1 %.2f\n\", b-a}'"
	sleep 20; echo /sbin/poweroff
	sleep 8
} | QEMU_SMP=$SMP QEMU_TIMEOUT=200 bash "$PORT/ramimage.sh" -d 2>&1
