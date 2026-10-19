#!/bin/sh
# Host stand of the WCN3990 driver's pure parts (minix/drivers/net/wcn).
# FW: where the tablet's firmware-5.bin and board-2.bin lie (outside the
# tree - their licence is not ours).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$HERE/../../../minix/drivers/net/wcn
FW=${FW:-/mnt/d/minix/clover-flash/pmos/fw/ath10k/WCN3990/hw1.0}
OUT=${OUT:-/tmp/wcn-stand}
mkdir -p "$OUT"
CC=${CC:-cc}
CFLAGS="-O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -I$SRC"

$CC $CFLAGS -o "$OUT/fwtest" "$HERE/fwtest.c" "$SRC/wcn_fw.c"
if [ -f "$FW/board-2.bin" ]; then
	"$OUT/fwtest" "$FW"
else
	echo "($FW: no files - the made-up cases only)"
	"$OUT/fwtest"
fi
