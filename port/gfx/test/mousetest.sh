#!/bin/sh
# meechowm's reading of the mouse on the host: mousetest.c over meechowm's
# mousesplit.c, fed by usb_mouse's report.c.
#
#   sh port/gfx/test/mousetest.sh [seed]
#   sh port/gfx/test/mousetest.sh -m	# and each mutation must be caught
#
# The mutations, each on a copy of mousesplit.c:
#   bound  no boundary between reports: the motion of a read is summed
#   le     a second X or Y does not begin a report (< for <=)
#   after  a report's motion goes after its buttons and its wheel
#   end    the end of a read does not end its report
HERE=$(cd "$(dirname "$0")" && pwd)
TOP=$HERE/../../..
SRC=$TOP/minix/usr.bin/meechowm
MOUSE=$TOP/minix/drivers/usb/usb_mouse
OUT=${TMPDIR:-/tmp}/mousetest.$$
trap 'rm -rf "$OUT"' EXIT
mkdir -p "$OUT/inc/minix"
# <minix/input.h> alone: the rest of minix/include would stand in front of
# the host's own headers.
cp "$TOP/minix/include/minix/input.h" "$OUT/inc/minix/"

build() {	# build OUTPUT MOUSESPLIT.C
	cc -O2 -Wall -Wextra -Werror -I"$OUT/inc" -I"$SRC" -I"$MOUSE" \
	    -o "$1" "$HERE/mousetest.c" "$2" "$MOUSE/report.c"
}

if [ "$1" != "-m" ]; then
	build "$OUT/mousetest" "$SRC/mousesplit.c" || exit 1
	"$OUT/mousetest" "$@"
	exit $?
fi

build "$OUT/mousetest" "$SRC/mousesplit.c" || exit 1
"$OUT/mousetest" || { echo "mousetest: fails unmutated"; exit 1; }
missed=0
for m in bound le after end; do
	case $m in
	bound)	e='s/if (r\.last != PLACE_NONE && p <= r\.last)/if (0)/' ;;
	le)	e='s/p <= r\.last/p < r.last/' ;;
	after)	e='/The report.s motion comes before/{n;d;}' ;;
	end)	e='s/^	end_report(&r, ops, arg);$/	(void)0;/' ;;
	esac
	sed -e "$e" "$SRC/mousesplit.c" > "$OUT/$m.c"
	if cmp -s "$SRC/mousesplit.c" "$OUT/$m.c"; then
		echo "mutation $m: did not apply"
		missed=$((missed + 1))
		continue
	fi
	build "$OUT/$m" "$OUT/$m.c" || { echo "mutation $m: no build";
	    missed=$((missed + 1)); continue; }
	if "$OUT/$m" > "$OUT/$m.out"; then
		echo "mutation $m: MISSED"
		missed=$((missed + 1))
	else
		echo "mutation $m: caught - $(grep -m1 FAIL "$OUT/$m.out")"
	fi
done
echo "mousetest: $missed mutation(s) not caught"
exit $((missed != 0))
