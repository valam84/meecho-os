#!/bin/bash
# Host tests of the touch driver: the QUP I2C master (qup.c), the
# touch-to-key logic (keys.c), and the whole bring-up on the Mi Pad 4's
# device tree (dttest.c - needs the tree, which is GPL-2.0-only and not
# kept here: CLOVER_DTB=, or what port/clover/mkboot.sh left in
# ~/work-clover-flash).
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
TREE=$(cd "$HERE/../../.." && pwd)
SRC=$TREE/minix/drivers/hid/qcom_touch
INC=$TREE/minix/include
T=${TMPDIR:-/tmp}
cc -std=c99 -Wall -Wextra -Werror -O1 -g -I"$SRC" -o "$T/quptest" \
    "$HERE/quptest.c" "$SRC/qup.c"
"$T/quptest"
if [ -f "$HERE/keystest.c" ]; then
	cc -std=c99 -Wall -Wextra -Werror -O1 -g -I"$SRC" -I"$INC" \
	    -o "$T/keystest" "$HERE/keystest.c" "$SRC/keys.c"
	"$T/keystest"
fi

CLOVER=${CLOVER_DTB:-$HOME/work-clover-flash/sdm660-xiaomi-clover.dtb}
if [ -f "$CLOVER" ]; then
	SD=$TREE/port/test/sdmmc
	cc -std=gnu99 -Wall -Wextra -Wno-unused-parameter -O1 -g \
	    -I"$HERE/stubs" -I"$SD/clkstubs" -I"$SD/stubs" -I"$SRC" \
	    -idirafter "$INC" \
	    -D__unused="__attribute__((unused))" \
	    -DREAL_FDT_H="\"$INC/minix/fdt.h\"" \
	    -o "$T/touch-dttest" "$HERE/dttest.c" "$SRC/qcom_soc.c" \
	    "$SRC/qup.c" "$SRC/keys.c" "$TREE/minix/lib/libsys/fdt.c"
	"$T/touch-dttest" "$CLOVER"
else
	echo "dttest: no $CLOVER - skipped (port/clover/mkboot.sh)"
fi
