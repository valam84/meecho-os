#!/bin/bash
# Host test of dwc3_ecm on the Mi Pad 4's device tree (dwtest.c): the probe,
# and the take-over against a model of the DWC3 core.  The tree is
# GPL-2.0-only and not kept here: CLOVER_DTB=, or what port/clover/mkboot.sh
# left in ~/work-clover-flash.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
TREE=$(cd "$HERE/../../.." && pwd)
SRC=${DWC3_SRC:-$TREE/minix/drivers/net/dwc3_ecm}
SOC=$TREE/minix/drivers/hid/qcom_touch
INC=$TREE/minix/include
SD=$TREE/port/test/sdmmc
T=${TMPDIR:-/tmp}/dwc3-dwtest.d
CLOVER=${CLOVER_DTB:-$HOME/work-clover-flash/sdm660-xiaomi-clover.dtb}
if [ ! -f "$CLOVER" ]; then
	echo "dwtest: no $CLOVER - skipped (port/clover/mkboot.sh)"
	exit 0
fi
mkdir -p "$T"
CC=(cc -std=gnu99 -Wall -Wextra -Wno-unused-parameter -O1 -g
    -I"$HERE/stubs" -I"$TREE/port/test/qcom_touch/stubs" -I"$SD/clkstubs"
    -I"$SD/stubs" -I"$SRC" -I"$SOC" -idirafter "$INC"
    "-D__unused=__attribute__((unused))"
    "-DREAL_FDT_H=\"$INC/minix/fdt.h\"")
# The core's register access goes to the model: dev-hook.h.
for f in dwc3_dev dwc3_ep0; do
	"${CC[@]}" -include "$HERE/dev-hook.h" -c -o "$T/$f.o" "$SRC/$f.c"
done
for f in dwc3_find dwc3_qcom qusb2; do
	"${CC[@]}" -c -o "$T/$f.o" "$SRC/$f.c"
done
"${CC[@]}" -c -o "$T/qcom_soc.o" "$SOC/qcom_soc.c"
"${CC[@]}" -c -o "$T/fdt.o" "$TREE/minix/lib/libsys/fdt.c"
"${CC[@]}" -c -o "$T/dwtest.o" "$HERE/dwtest.c"
"${CC[@]}" -o "$T/dwtest" "$T"/*.o
"$T/dwtest" "$CLOVER"
