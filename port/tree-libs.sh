#!/bin/bash
# Build and install the C run-time and libc for evbarm64 into DESTDIR:
# the stage 1 goal. Run after tree-includes.sh.
#
#	bash tree-libs.sh [dir ...]	default: lib/csu lib/libc
#
# Each directory gets "obj" first: without an object directory nbmake
# builds in the source tree and litters it with products.
set -uo pipefail
PORT=$(cd "$(dirname "$0")" && pwd)
SRCDIR=${SRCDIR:-$(cd "$PORT/.." && pwd)}

M=${M:-$SRCDIR}
MAKE=$HOME/tools-evbarm64/bin/nbmake-evbarm64-el
JOBS=${JOBS:-24}
LOG=$HOME/build-libs.log
DIRS=("$@")
[ ${#DIRS[@]} -eq 0 ] && DIRS=(lib/csu lib/libc)
cd "$M"
: > "$LOG"
for d in "${DIRS[@]}"; do
	echo "=== $d" | tee -a "$LOG"
	$MAKE -C "$d" obj >> "$LOG" 2>&1 || { echo "FAILED: $d obj"; break; }
	$MAKE -C "$d" -j$JOBS dependall >> "$LOG" 2>&1 || { echo "FAILED: $d dependall"; break; }
	$MAKE -C "$d" install >> "$LOG" 2>&1 || { echo "FAILED: $d install"; break; }
	echo "ok: $d"
done
grep -n -m1 -B8 -A12 -E "error:|Error code|\*\*\* " "$LOG" || echo "no errors in log"
