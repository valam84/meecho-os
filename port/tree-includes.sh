#!/bin/bash
# Populate DESTDIR with the directory tree and the headers, the two build
# steps of "distribution" that come before any library. Run after
# build-minix.sh evbarm64-el tools.
set -uo pipefail
PORT=$(cd "$(dirname "$0")" && pwd)
SRCDIR=${SRCDIR:-$(cd "$PORT/.." && pwd)}

M=${M:-$SRCDIR}
MAKE=$HOME/tools-evbarm64/bin/nbmake-evbarm64-el
JOBS=${JOBS:-24}
LOG=$HOME/build-includes.log
cd "$M"
{
	echo "=== do-distrib-dirs"
	$MAKE -j$JOBS do-distrib-dirs || exit 1
	echo "=== includes"
	$MAKE -j$JOBS includes || exit 1
	echo "=== done"
} > "$LOG" 2>&1
rc=$?
echo "rc=$rc"
grep -n -m1 -B5 -A15 -E "error:|ERROR:|\*\*\* |Error code" "$LOG" || true
tail -5 "$LOG"
exit $rc
