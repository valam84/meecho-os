#!/bin/bash
# Собрать и поставить перечисленные каталоги; статус проверяется здесь,
# а не в строке для wsl.exe (там $? всегда ноль).
set -uo pipefail
PORT=$(cd "$(dirname "$0")" && pwd)
SRCDIR=${SRCDIR:-$(cd "$PORT/.." && pwd)}

M=$SRCDIR
MAKE=$HOME/tools-evbarm64/bin/nbmake-evbarm64-el
JOBS=${JOBS:-48}
cd "$M" || exit 1
rc=0
for d in "$@"; do
	log=/tmp/mk-$(echo "$d" | tr / _).log
	{ $MAKE -C "$d" obj && $MAKE -C "$d" -j"$JOBS" dependall && $MAKE -C "$d" install; } > "$log" 2>&1
	if [ $? -eq 0 ]; then echo "ok   $d"; else
		echo "FAIL $d  ($log)"
		grep -m3 -E "error:|Error code|cannot find|don't know how" "$log" | sed "s|$M/||" | cut -c1-160
		rc=1
	fi
done
exit $rc
