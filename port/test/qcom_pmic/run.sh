#!/bin/bash
# Host test of the SPMI PMIC arbiter client (qcom_pmic/spmi.c).
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$HERE/../../../minix/drivers/power/qcom_pmic
OUT=${TMPDIR:-/tmp}/spmitest
cc -std=c99 -Wall -Wextra -Werror -O1 -g -I"$SRC" -o "$OUT" \
    "$HERE/spmitest.c" "$SRC/spmi.c"
"$OUT"

# The arbiter discovery on the Mi Pad 4's tree (GPL-2.0-only, not kept here).
TREE=$(cd "$HERE/../../.." && pwd)
CLOVER=${CLOVER_DTB:-$HOME/work-clover-flash/sdm660-xiaomi-clover.dtb}
if [ -f "$CLOVER" ]; then
	SD=$TREE/port/test/sdmmc
	TS=$TREE/port/test/qcom_touch/stubs
	cc -std=gnu99 -Wall -Wextra -Wno-unused-parameter -O1 -g \
	    -I"$TS" -I"$SD/clkstubs" -I"$SD/stubs" -I"$SRC" \
	    -idirafter "$TREE/minix/include" \
	    -D__unused="__attribute__((unused))" \
	    -DREAL_FDT_H="\"$TREE/minix/include/minix/fdt.h\"" \
	    -o "${TMPDIR:-/tmp}/pmic-dttest" "$HERE/dttest.c" "$SRC/spmi.c" \
	    "$TREE/minix/lib/libsys/fdt.c"
	"${TMPDIR:-/tmp}/pmic-dttest" "$CLOVER"
else
	echo "dttest: no $CLOVER - skipped (port/clover/mkboot.sh)"
fi
