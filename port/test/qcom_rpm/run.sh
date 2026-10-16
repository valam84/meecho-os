#!/bin/bash
# Host stand of qcom_rpm's GLINK client against a model of the RPM.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$HERE/../../../minix/drivers/power/qcom_rpm
OUT=${TMPDIR:-/tmp}/rpmtest
cc -std=c99 -Wall -Wextra -Werror -O1 -g -I"$SRC" -o "$OUT" \
    "$HERE/rpmtest.c" "$SRC/glink.c"
"$OUT"

# The walk of the Mi Pad 4's tree (GPL-2.0-only, not kept here).
TREE=$(cd "$HERE/../../.." && pwd)
CLOVER=${CLOVER_DTB:-$HOME/work-clover-flash/sdm660-xiaomi-clover.dtb}
if [ -f "$CLOVER" ]; then
	SD=$TREE/port/test/sdmmc
	TS=$TREE/port/test/qcom_touch/stubs
	cc -std=gnu99 -Wall -Wextra -Wno-unused-parameter -O1 -g \
	    -I"$HERE/stubs" -I"$TS" -I"$SD/clkstubs" -I"$SD/stubs" -I"$SRC" \
	    -idirafter "$TREE/minix/include" \
	    -D__unused="__attribute__((unused))" \
	    -DREAL_FDT_H="\"$TREE/minix/include/minix/fdt.h\"" \
	    -o "${TMPDIR:-/tmp}/rpm-dttest" "$HERE/dttest.c" "$SRC/glink.c" \
	    "$TREE/minix/lib/libsys/fdt.c"
	"${TMPDIR:-/tmp}/rpm-dttest" "$CLOVER"
else
	echo "dttest: no $CLOVER - skipped (port/clover/mkboot.sh)"
fi
