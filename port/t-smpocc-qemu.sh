#!/bin/sh
# Занятость ядер на QEMU — тем же способом, что и на плате: суммой cycles из
# psinfo за окно реального времени. Нужно, чтобы отделить «плата» от «система»:
# потолок занятости, найденный на CB2, либо повторится здесь, либо нет.
#
# Сравнивать между собой QEMU с четырьмя ядрами и QEMU с одним нельзя вовсе —
# под TCG одиночная задача замедляется, когда эмулятор крутит четыре потока
# vCPU. Занятость этого не требует: она считается внутри одной конфигурации.
#
# Гость печатает сырьё, а сумма считается здесь. Так вышло не от любви к
# лишнему шагу: команда с awk внутри echo внутри ssh внутри bash не пережила
# трёх уровней кавычек и приехала в гостя покалеченной, а в консоль ещё и не
# влезает — канонический буфер tty режет строку около 255 байт.
#
#	t-smpocc-qemu.sh [ядер] [задач]
PORT=$(cd "$(dirname "$0")" && pwd)
SRCDIR=${SRCDIR:-$(cd "$PORT/.." && pwd)}

SMP=${1:-4}
JOBS=${2:-6}
N=${N:-12000}
LOG=${LOG:-/tmp/qocc.log}

{
	sleep 34; echo root
	sleep 3;  echo "j=0; while [ \$j -lt $JOBS ]; do (n=0; while [ \$n -lt $N ]; do n=\$((n+1)); done) & j=\$((j+1)); done"
	sleep 4;  echo 'echo MARK-A; cat /proc/uptime; cat /proc/[0-9]*/psinfo; echo MARK-END'
	sleep 12; echo 'echo MARK-B; cat /proc/uptime; cat /proc/[0-9]*/psinfo; echo MARK-END'
	sleep 60; echo /sbin/poweroff
	sleep 8
} | QEMU_SMP=$SMP QEMU_TIMEOUT=200 bash "$PORT/ramimage.sh" -d > "$LOG" 2>&1

python3 - "$LOG" <<'PY'
import re, sys

blocks, cur, name = {}, None, None
for line in open(sys.argv[1], errors='replace'):
	line = line.strip()
	if line.startswith('MARK-A') or line.startswith('MARK-B'):
		name, cur = line[5], []
		continue
	if line.startswith('MARK-END'):
		if name and cur:
			blocks[name] = cur
		name, cur = None, None
		continue
	if cur is not None:
		cur.append(line)

def parse(lines):
	"""Первая строка блока — uptime, дальше psinfo: cycles в десятом поле."""
	t, total = None, 0
	for l in lines:
		f = l.split()
		if t is None and len(f) == 1:
			try:
				t = float(f[0])
			except ValueError:
				pass
			continue
		if len(f) >= 10 and f[0] == '1':
			try:
				total += int(f[9])
			except ValueError:
				pass
	return t, total

if 'A' in blocks and 'B' in blocks:
	ta, ca = parse(blocks['A'])
	tb, cb = parse(blocks['B'])
	if ta is not None and tb is not None and tb > ta:
		print("occupancy: %.0f cycles / %.2f s = %.0f cyc/s" %
		      (cb - ca, tb - ta, (cb - ca) / (tb - ta)))
	else:
		print("не разобрал время: A=%s B=%s" % (ta, tb))
else:
	print("не нашёл оба замера в %s" % sys.argv[1])
PY
