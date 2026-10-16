#!/bin/bash
# Host test of dwc3_ecm's probe on the Mi Pad 4's device tree (dwtest.c).
# The tree is GPL-2.0-only and not kept here: CLOVER_DTB=, or what
# port/clover/mkboot.sh left in ~/work-clover-flash.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
TREE=$(cd "$HERE/../../.." && pwd)
SRC=$TREE/minix/drivers/net/dwc3_ecm
SOC=$TREE/minix/drivers/hid/qcom_touch
INC=$TREE/minix/include
SD=$TREE/port/test/sdmmc
T=${TMPDIR:-/tmp}
CLOVER=${CLOVER_DTB:-$HOME/work-clover-flash/sdm660-xiaomi-clover.dtb}
if [ ! -f "$CLOVER" ]; then
	echo "dwtest: no $CLOVER - skipped (port/clover/mkboot.sh)"
	exit 0
fi
cc -std=gnu99 -Wall -Wextra -Wno-unused-parameter -O1 -g \
    -I"$TREE/port/test/qcom_touch/stubs" -I"$SD/clkstubs" -I"$SD/stubs" \
    -I"$SRC" -I"$SOC" -idirafter "$INC" \
    -D__unused="__attribute__((unused))" \
    -DREAL_FDT_H="\"$INC/minix/fdt.h\"" \
    -o "$T/dwc3-dwtest" "$HERE/dwtest.c" "$SRC/dwc3_find.c" \
    "$SRC/dwc3_qcom.c" "$SRC/qusb2.c" "$SOC/qcom_soc.c" \
    "$TREE/minix/lib/libsys/fdt.c"
"$T/dwc3-dwtest" "$CLOVER"
