#!/bin/bash
# Host tests of the touch driver: the QUP I2C master (qup.c), the
# touch-to-key logic (keys.c over libosk), and the whole bring-up on the Mi Pad 4's
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
# keys.c over libosk.  <minix/libosk.h> alone: the rest of minix/include
# would stand in front of the host's own headers.
OSK=$TREE/minix/lib/libosk
mkdir -p "$T/keystest-inc/minix"
cp "$INC/minix/libosk.h" "$T/keystest-inc/minix/"
cc -std=c99 -Wall -Wextra -Werror -O1 -g -I"$SRC" -I"$T/keystest-inc" \
    -o "$T/keystest" "$HERE/keystest.c" "$SRC/keys.c" "$OSK/osk.c" \
    "$OSK/osk_layout.c"
"$T/keystest"
# desk.c - the desktop's touches - through meechowm's own reader of them
# (touchsplit.c): the driver's frames and the compositor's reading checked
# against each other.  <minix/input.h> alone, as above.
cp "$INC/minix/input.h" "$T/keystest-inc/minix/"
cc -std=c99 -Wall -Wextra -Werror -O1 -g -I"$SRC" -I"$T/keystest-inc" \
    -I"$TREE/minix/usr.bin/meechowm" -o "$T/desktest" "$HERE/desktest.c" \
    "$SRC/desk.c" "$TREE/minix/usr.bin/meechowm/touchsplit.c"
"$T/desktest"

CLOVER=${CLOVER_DTB:-$HOME/work-clover-flash/sdm660-xiaomi-clover.dtb}
if [ -f "$CLOVER" ]; then
	SD=$TREE/port/test/sdmmc
	cc -std=gnu99 -Wall -Wextra -Wno-unused-parameter -O1 -g \
	    -I"$HERE/stubs" -I"$SD/clkstubs" -I"$SD/stubs" -I"$SRC" \
	    -idirafter "$INC" \
	    -D__unused="__attribute__((unused))" \
	    -DREAL_FDT_H="\"$INC/minix/fdt.h\"" \
	    -o "$T/touch-dttest" "$HERE/dttest.c" "$SRC/qcom_soc.c" \
	    "$SRC/qup.c" "$SRC/keys.c" "$SRC/desk.c" "$OSK/osk.c" \
	    "$OSK/osk_layout.c" "$TREE/minix/lib/libsys/fdt.c"
	"$T/touch-dttest" "$CLOVER"
else
	echo "dttest: no $CLOVER - skipped (port/clover/mkboot.sh)"
fi
