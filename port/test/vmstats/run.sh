#!/bin/sh
# memstats() of minix/servers/vm/alloc.c - the function itself, cut out of
# the file - against a walk bit by bit (what it was before 2026-10-19) on
# random bitmaps: runs of every length, chunks all free and all used, runs
# across chunks, the first and last page.  Host gcc; prints "ok N".
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$HERE/../../../minix/servers/vm/alloc.c
T=${TMPDIR:-/tmp}/vmstats.$$
mkdir -p "$T" || exit 1
trap 'rm -rf "$T"' EXIT
sed -n '/^void memstats(int \*nodes, int \*pages, int \*largest)$/,/^}$/p' \
    "$SRC" > "$T/memstats.c"
[ -s "$T/memstats.c" ] || { echo "FAIL: memstats() not found in $SRC"; exit 1; }
gcc -O2 -Wall -o "$T/t" -include "$HERE/shim.h" "$HERE/test.c" \
    "$T/memstats.c" && "$T/t"
