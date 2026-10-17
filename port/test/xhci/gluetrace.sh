#!/bin/bash
# The xHCI glue stand (gluetest.c): what the driver writes to the SoC to
# bring its controller up, as a trace, on the machine's own device tree.
#
#   bash gluetrace.sh		both machines, below
#   bash gluetrace.sh -v	the same, with the driver's log
#
# The CB2: the trace of the tree against the trace of the driver at
# $OLD_REV, both of its controllers - a glue moved into a table must write
# the same registers, in the same order.
#
# The Mi Pad 4 (port/clover/hw-usbhost.md): its tree as mainline has it
# says dr_mode "peripheral" and nothing may be touched; with the tablet's
# fix-up (dr_mode "otg", as mkboot.sh builds it) the glue takes the block
# over as the boot loader left it and writes exactly what the plan says -
# the device events masked, the core into host mode, the session override
# off, and on the way out the core back into device mode; the GCC, the PHY
# and the fuse never.  A block the boot loader did not leave running (power
# domain off, PLL not locked) is not taken over, and nothing is written.
#
# CB2_DTB is the CB2's tree (default ~/cb2-hdmi.dtb, what MEECHO boots
# with); OLD_REV the commit before the glue became a table; CLOVER_DTB the
# Mi Pad 4's as mkboot.sh builds it from mainline (GPL-2.0-only, not kept
# here).
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
TREE=$(cd "$HERE/../../.." && pwd)
SRC=${XHCI_SRC:-$TREE/minix/drivers/usb/xhci}
ECM=${DWC3_SRC:-$TREE/minix/drivers/net/dwc3_ecm}
SOC=$TREE/minix/drivers/hid/qcom_touch
SD=$TREE/port/test/sdmmc
T=${TMPDIR:-/tmp}/xhci-glue
CB2=${CB2_DTB:-$HOME/cb2-hdmi.dtb}
CLOVER=${CLOVER_DTB:-$HOME/work-clover-flash/sdm660-xiaomi-clover.dtb}
OLD_REV=${OLD_REV:-653743f20}
V=
[ "${1:-}" = -v ] && V=-v

mkdir -p "$T/old" "$T/new"

CC=(cc -std=gnu99 -Wall -Wextra -Wno-unused-parameter -O1 -g
    -I"$HERE/gluestubs" -I"$TREE/port/test/dwc3_ecm/stubs"
    -I"$TREE/port/test/qcom_touch/stubs" -I"$SD/clkstubs" -I"$SD/stubs"
    -idirafter "$TREE/minix/include"
    "-D__unused=__attribute__((unused))"
    "-DREAL_FDT_H=\"$TREE/minix/include/minix/fdt.h\"")

# The driver as it was: its own header and the two files of its glue.
for f in xhci.h xhcireg.h xhci_find.c xhci_rk.c; do
	git -C "$TREE" show "$OLD_REV:minix/drivers/usb/xhci/$f" > "$T/old/$f"
done
rm -f "$T"/old/*.o "$T"/new/*.o
for f in xhci_find xhci_rk; do
	"${CC[@]}" -DOLD_API -I"$T/old" -include "$HERE/gluehook.h" \
	    -c -o "$T/old/$f.o" "$T/old/$f.c"
done
"${CC[@]}" -DOLD_API -I"$T/old" -c -o "$T/old/gluetest.o" "$HERE/gluetest.c"
"${CC[@]}" -c -o "$T/old/fdt.o" "$TREE/minix/lib/libsys/fdt.c"
cc -o "$T/old/gluetest" "$T"/old/*.o

# And as it is, with Qualcomm's glue and the dwc3_ecm files it borrows.
NEWCC=("${CC[@]}" -I"$SRC" -I"$ECM" -I"$SOC")
for f in xhci_find xhci_rk xhci_dwc3 xhci_qcom; do
	"${NEWCC[@]}" -include "$HERE/gluehook.h" \
	    -c -o "$T/new/$f.o" "$SRC/$f.c"
done
for f in "$ECM/dwc3_find.c" "$ECM/dwc3_qcom.c" "$ECM/qusb2.c" \
    "$SOC/qcom_soc.c" "$HERE/gluetest.c"; do
	"${NEWCC[@]}" -c -o "$T/new/$(basename "$f" .c).o" "$f"
done
cp "$T/old/fdt.o" "$T/new/fdt.o"
cc -o "$T/new/gluetest" "$T"/new/*.o

fail=0

# One difference is meant.  The old glue cleared SUSPHY on whichever
# controller it drove; the core's quirks now come from its node, and the
# CB2's second controller (usb@fd000000, the one rc does not start) has no
# snps,dis_u2_susphy_quirk - the vendor system leaves SUSPHY set there, and
# so does the driver now.  Its old trace is edited to say exactly that.
expect() {
	if [ "$1" = 1 ]; then
		sed 's/^\(wr 0xfd000000+0xc200 = \)0x00101408$/\10x00101448/'
	else
		cat
	fi
}

if [ -f "$CB2" ]; then
	for inst in 0 1; do
		"$T/old/gluetest" $V "$CB2" $inst | expect $inst \
		    > "$T/old-$inst.trace"
		"$T/new/gluetest" $V "$CB2" $inst > "$T/new-$inst.trace"
		n=$(grep -c . "$T/new-$inst.trace" || true)
		w=$(grep -c '^wr ' "$T/new-$inst.trace" || true)
		if cmp -s "$T/old-$inst.trace" "$T/new-$inst.trace"; then
			echo "CB2 controller $inst: same trace ($n lines," \
			    "$w writes)"
		else
			echo "CB2 controller $inst: DIFFERENT trace"
			diff "$T/old-$inst.trace" "$T/new-$inst.trace" |
			    head -20
			fail=1
		fi
	done
else
	echo "gluetrace: no $CB2 - the CB2 skipped"
fi

if [ ! -f "$CLOVER" ]; then
	echo "gluetrace: no $CLOVER - the Mi Pad 4 skipped (port/clover/mkboot.sh)"
	exit $fail
fi

check() {	# what file grep-arguments...: the trace must match
	local what=$1 file=$2
	shift 2
	if grep -q "$@" "$file"; then
		return 0
	fi
	echo "  FAIL $what"
	fail=1
}
nocheck() {	# what file grep-arguments...: the trace must not match
	local what=$1 file=$2
	shift 2
	if grep -q "$@" "$file"; then
		echo "  FAIL $what"
		grep "$@" "$file" | head -5 | sed 's/^/    /'
		fail=1
	fi
}

# The tree as mainline has it: dr_mode "peripheral".
"$T/new/gluetest" $V -q on -s "$CLOVER" 0 > "$T/mipad-mainline.trace"
if [ "$(cat "$T/mipad-mainline.trace")" = "find 6" ]; then
	echo "Mi Pad 4, mainline tree: refused, nothing mapped"
else
	echo "Mi Pad 4, mainline tree: FAIL"
	sed 's/^/    /' "$T/mipad-mainline.trace" | head
	fail=1
fi

# With the tablet's fix-up, as the image carries it.
cp "$CLOVER" "$T/clover-otg.dtb"
fdtput -t s "$T/clover-otg.dtb" /soc@0/usb@a8f8800/usb@a800000 dr_mode otg
CORE=0xa800000
QS=0xa8f8800

f=$T/mipad-on.trace
"$T/new/gluetest" $V -d -q on -s "$T/clover-otg.dtb" 0 > "$f"
check "events masked" "$f" -x "wr $CORE+0xc408 = 0x80001000"
check "soft reset held" "$f" -x "wr $CORE+0xc110 = 0x00112800"
check "host mode" "$f" -x "wr $CORE+0xc110 = 0x00111000"
check "PHY interface left as the boot loader's" "$f" \
    -x "wr $CORE+0xc200 = 0x00002400"
check "override off: HS" "$f" -x "final $QS+0x0010 = 0x00000000 (was 0x10100000)"
check "override off: SS" "$f" -x "final $QS+0x0030 = 0x00000000 (was 0x01000000)"
check "back to device" "$f" -x "wr $CORE+0xc110 = 0x00112000"
other=$(grep -E '^(wr|final) ' "$f" | grep -v -e " $CORE+" -e " $QS+" || true)
[ -z "$other" ] ||
    { echo "  FAIL a write outside the core and the wrapper: $other"; fail=1; }
first=$(grep -m1 "^wr $CORE" "$f" || true)
[ "$first" = "wr $CORE+0xc408 = 0x80001000" ] ||
    { echo "  FAIL the first write is not the event mask: $first"; fail=1; }
order=$(grep -n -e "^report" -e "^down" -e "0x00111000" -e "= 0x00112000" "$f" |
    cut -d: -f2 | tr '\n' ' ')
[ "$order" = "wr $CORE+0xc110 = 0x00111000 report down wr $CORE+0xc110 = 0x00112000 " ] ||
    { echo "  FAIL order: $order"; fail=1; }
[ $fail = 0 ] && echo "Mi Pad 4, as the boot loader leaves it:" \
    "$(grep -c '^wr' "$f") writes, all as planned"

for mode in off nolock; do
	f=$T/mipad-$mode.trace
	"$T/new/gluetest" $V -q $mode -s "$T/clover-otg.dtb" 0 > "$f"
	if grep -q -x "up 6" "$f" && ! grep -q -E "^(wr|final) " "$f"; then
		echo "Mi Pad 4, $mode: refused, nothing written"
	else
		echo "Mi Pad 4, $mode: FAIL"
		sed 's/^/    /' "$f" | head
		fail=1
	fi
done
nocheck "the core mapped with its power domain off" "$T/mipad-off.trace" \
    "^map $CORE"

exit $fail
