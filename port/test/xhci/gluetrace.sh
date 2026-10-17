#!/bin/bash
# The xHCI glue stand (gluetest.c): what the driver writes to the SoC to
# bring its controller up, as a trace, on the machine's own device tree.
#
#   bash gluetrace.sh		the CB2: the trace of the tree against the
#				trace of the driver at $OLD_REV, both of its
#				controllers - a glue moved into a table must
#				write the same registers, in the same order
#   bash gluetrace.sh -v	the same, with the driver's log
#
# CB2_DTB is the CB2's tree (default ~/cb2-hdmi.dtb, what MEECHO boots
# with); OLD_REV the commit before the glue became a table.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
TREE=$(cd "$HERE/../../.." && pwd)
SRC=$TREE/minix/drivers/usb/xhci
SD=$TREE/port/test/sdmmc
T=${TMPDIR:-/tmp}/xhci-glue
CB2=${CB2_DTB:-$HOME/cb2-hdmi.dtb}
OLD_REV=${OLD_REV:-653743f20}
V=
[ "${1:-}" = -v ] && V=-v

if [ ! -f "$CB2" ]; then
	echo "gluetrace: no $CB2 - skipped"
	exit 0
fi
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

# And as it is.
for f in xhci_find xhci_rk xhci_dwc3; do
	"${CC[@]}" -I"$SRC" -include "$HERE/gluehook.h" \
	    -c -o "$T/new/$f.o" "$SRC/$f.c"
done
"${CC[@]}" -I"$SRC" -c -o "$T/new/gluetest.o" "$HERE/gluetest.c"
cp "$T/old/fdt.o" "$T/new/fdt.o"
cc -o "$T/new/gluetest" "$T"/new/*.o

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

fail=0
for inst in 0 1; do
	"$T/old/gluetest" $V "$CB2" $inst | expect $inst > "$T/old-$inst.trace"
	"$T/new/gluetest" $V "$CB2" $inst > "$T/new-$inst.trace"
	n=$(grep -c . "$T/new-$inst.trace" || true)
	w=$(grep -c '^wr ' "$T/new-$inst.trace" || true)
	if cmp -s "$T/old-$inst.trace" "$T/new-$inst.trace"; then
		echo "CB2 controller $inst: same trace ($n lines, $w writes)"
	else
		echo "CB2 controller $inst: DIFFERENT trace"
		diff "$T/old-$inst.trace" "$T/new-$inst.trace" | head -20
		fail=1
	fi
done
exit $fail
