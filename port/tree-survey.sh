#!/bin/bash
# Веха 8.3, шаг 1: пройти по дереву и выяснить, что собирается под aarch64.
# Каждый каталог собирается отдельно, отказ одного не мешает остальным.
set -uo pipefail
PORT=$(cd "$(dirname "$0")" && pwd)
SRCDIR=${SRCDIR:-$(cd "$PORT/.." && pwd)}

M=$SRCDIR
MAKE=$HOME/tools-evbarm64/bin/nbmake-evbarm64-el
JOBS=${JOBS:-48}
OUT=${OUT:-$HOME/survey}
mkdir -p "$OUT/logs"
: > "$OUT/result.txt"
cd "$M" || exit 1

for top in "$@"; do
	for d in $(ls -1 "$M/$top" 2>/dev/null); do
		[ -f "$M/$top/$d/Makefile" ] || continue
		name="$top/$d"
		log="$OUT/logs/$(echo "$name" | tr / _).log"
		{
			$MAKE -C "$name" obj
			$MAKE -C "$name" -k -j"$JOBS" dependall
		} > "$log" 2>&1
		rc=$?
		if [ $rc -eq 0 ]; then
			$MAKE -C "$name" -k install >> "$log" 2>&1
			irc=$?
			if [ $irc -eq 0 ]; then echo "OK       $name" >> "$OUT/result.txt"
			else echo "INSTFAIL $name" >> "$OUT/result.txt"; fi
		else
			echo "FAIL     $name" >> "$OUT/result.txt"
		fi
	done
done
echo "=== done"
sort "$OUT/result.txt" | awk '{print $1}' | uniq -c
