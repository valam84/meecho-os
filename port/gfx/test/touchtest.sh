#!/bin/sh
# meechowm's reading of a touch screen on the host: touchtest.c over
# meechowm's touchsplit.c.
#
#   sh port/gfx/test/touchtest.sh [seed]
#   sh port/gfx/test/touchtest.sh -m	# and each mutation must be caught
#
# The mutations, each on a copy of touchsplit.c:
#   rel     a mouse's relative X and Y taken for a touch's
#   digi    the digitizer page left among the mouse's events
#   still   a contact that did not move is said to move
#   frame   no frame after a read
#   clamp   X and Y past the screen not clamped
HERE=$(cd "$(dirname "$0")" && pwd)
TOP=$HERE/../../..
SRC=$TOP/minix/usr.bin/meechowm
OUT=${TMPDIR:-/tmp}/touchtest.$$
trap 'rm -rf "$OUT"' EXIT
mkdir -p "$OUT/inc/minix"
# <minix/input.h> alone: the rest of minix/include would stand in front of
# the host's own headers.
cp "$TOP/minix/include/minix/input.h" "$OUT/inc/minix/"

build() {	# build OUTPUT TOUCHSPLIT.C
	cc -O2 -Wall -Wextra -Werror -Wno-unused-parameter -I"$OUT/inc" \
	    -I"$SRC" -o "$1" "$HERE/touchtest.c" "$2"
}

if [ "$1" != "-m" ]; then
	build "$OUT/touchtest" "$SRC/touchsplit.c" || exit 1
	"$OUT/touchtest" "$@"
	exit $?
fi

build "$OUT/touchtest" "$SRC/touchsplit.c" || exit 1
"$OUT/touchtest" || { echo "touchtest: fails unmutated"; exit 1; }
missed=0
for m in rel digi still frame clamp; do
	case $m in
	rel)	e='s/!(e->flags & INPUT_FLAG_REL)/1/' ;;
	digi)	e='s/e->page == INPUT_PAGE_DIGITIZER ||/0 ||/' ;;
	still)	e='s/t->at_x\[id\] != t->x || t->at_y\[id\] != t->y/1/' ;;
	frame)	e='s/if (acted)/if (acted \&\& 0)/' ;;
	clamp)	e='s/if (v > INPUT_DIGI_MAX)/if (0)/' ;;
	esac
	sed -e "$e" "$SRC/touchsplit.c" > "$OUT/$m.c"
	if cmp -s "$SRC/touchsplit.c" "$OUT/$m.c"; then
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
echo "touchtest: $missed mutation(s) not caught"
exit $((missed != 0))
