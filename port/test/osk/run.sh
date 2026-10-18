#!/bin/sh
# libosk on the host: osktest.c over minix/lib/libosk.
#
#   sh port/test/osk/run.sh		# the test
#   sh port/test/osk/run.sh -m	# and each mutation of osk.c must be caught
#
# The mutations, each on a copy of osk.c:
#   armed     an armed modifier stays after its key
#   lock      a second tap lets a modifier go instead of locking it
#   keyup     a key never comes up
#   heldmod   Ctrl pressed with a key never comes up
#   upper     the upper layer's keys go without Shift
#   layer     an armed Shift survives leaving the letters
#   edge      a key's right edge belongs to it
#   word      Ctrl is read from Shift's bits of the word
#   pressed   no key ever shows pressed
#   missed    a second down on a touch does not let the first key go
#   place     a key held from another layer does not show
HERE=$(cd "$(dirname "$0")" && pwd)
TOP=$HERE/../../..
SRC=$TOP/minix/lib/libosk
OUT=${TMPDIR:-/tmp}/osktest.$$
trap 'rm -rf "$OUT"' EXIT
mkdir -p "$OUT/inc/minix"
# <minix/libosk.h> alone: the rest of minix/include would stand in front of
# the host's own headers.
cp "$TOP/minix/include/minix/libosk.h" "$OUT/inc/minix/"

build() {	# build OUTPUT OSK.C
	cc -std=c99 -O2 -Wall -Wextra -Werror -I"$OUT/inc" -o "$1" \
	    "$HERE/osktest.c" "$2" "$SRC/osk_layout.c"
}

build "$OUT/osktest" "$SRC/osk.c" || exit 1
if [ "$1" != "-m" ]; then
	"$OUT/osktest"
	exit $?
fi
"$OUT/osktest" > "$OUT/clean.out" || { cat "$OUT/clean.out";
    echo "osktest: fails unmutated"; exit 1; }

missed=0
for m in armed lock keyup heldmod upper layer edge word pressed \
    missed place; do
	case $m in
	armed)	e='s/^		if (o->mod\[i\] == OSK_ARMED)$/		if (0)/' ;;
	lock)	e='s/OSK_ARMED ? OSK_LOCKED :/OSK_ARMED ? OSK_OFF :/' ;;
	keyup)	e='s/o->emit(o->arg, k->usage, 0);/(void)0;/' ;;
	heldmod) e='s/o->emit(o->arg, mod_usage\[OSK_CTRL\], 0);/(void)0;/' ;;
	upper)	e='s/(shown == OSK_UPPER \&\& !(k->flags \& OSK_K_PLAIN))/(0)/' ;;
	layer)	e='s/^		if (o->mod\[OSK_SHIFT\] == OSK_ARMED)$/		if (0)/' ;;
	edge)	e='s/x >= x0 \&\& x < x1/x >= x0 \&\& x <= x1/' ;;
	word)	e='s/(word >> 6) \& 3/(word >> 4) \& 3/' ;;
	pressed) e='s/o->t\[i\].seq > best/o->t[i].seq < best/' ;;
	missed)	e='s/release(o, id);	\/\* a down we missed/(void)0;	\/* a down we missed/' ;;
	place)	e='s/l->keys\[i\].row == k->row \&\& l->keys\[i\].x == k->x/\&l->keys[i] == k/' ;;
	esac
	sed -e "$e" "$SRC/osk.c" > "$OUT/$m.c"
	if cmp -s "$SRC/osk.c" "$OUT/$m.c"; then
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
echo "osktest: $missed mutation(s) not caught"
exit $((missed != 0))
