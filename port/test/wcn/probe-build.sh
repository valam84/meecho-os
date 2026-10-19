#!/bin/sh
# Build wlfwprobe for MEECHO (aarch64) with the cross compiler against
# DESTDIR; the result goes to $OUT, not to DESTDIR (as board-cc.sh).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$HERE/../../../minix/drivers/net/wcn
DEST=${DESTDIR:-$HOME/dest-evbarm64}
XCC=${XCC:-$HOME/xtools-aarch64/bin/aarch64-elf64-minix-gcc}
OUT=${OUT:-$HOME/obj-evbarm64/work/cc}
mkdir -p "$OUT"
$XCC -O -static -Wall -Wextra --sysroot="$DEST" -I"$SRC" \
    -o "$OUT/wlfwprobe" "$HERE/wlfwprobe.c" "$SRC/wcn_wlfw.c"
ls -l "$OUT/wlfwprobe"
