#!/bin/sh
# Собрать каталоги, которые ramdisk только копирует, а не строит.
set -u
PORT=$(cd "$(dirname "$0")" && pwd)
SRCDIR=${SRCDIR:-$(cd "$PORT/.." && pwd)}

M=$HOME/tools-evbarm64/bin/nbmake-evbarm64-el
cd $SRCDIR || exit 1
rc=0
for d in "$@"; do
	"$M" -C "$d" obj > /tmp/bt-obj.log 2>&1
	if ! "$M" -C "$d" -j8 dependall > /tmp/bt.log 2>&1; then
		echo "ОТКАЗ: $d"
		grep -nE "error:|Error:|\*\*\*" /tmp/bt.log | head -15
		tail -8 /tmp/bt.log
		rc=1
	else
		echo "ок: $d"
	fi
done
exit $rc
