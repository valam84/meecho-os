#!/bin/sh
#
# Does the stand actually catch anything?
#
# A set of checks that passes is worth nothing until it has been shown to
# fail.  This puts each of the driver's four historical defects back, one
# at a time, into a COPY of the driver, and runs the stand against it.
# Every one of them must be caught; a mutation that survives is a check
# that is decoration.
#
# The defects are the real ones, in the words of the commits that fixed
# them:
#
#   link     the Link TRB handed to the controller without the cycle bit
#            it is looking for - 255 commands through, the next 45 silent
#   first    keeping the LAST event of the wanted kind rather than the
#            first - every short read reported as a full one
#   isp      no interrupt-on-short-packet on the data stage - the same
#            symptom, from the other end
#   doorbell a transfer queued and never announced - no event at all, and
#            nothing pending in the controller
#
#   sh mutate.sh

set -e

HERE=${HERE:-$(cd "$(dirname "$0")" && pwd)}
SRC=${SRC:-$HOME/minix-src/minix/drivers/usb/xhci}
WORK=${WORK:-/tmp/xhcimutate}

rm -rf "$WORK"
mkdir -p "$WORK"
cp "$SRC"/*.c "$SRC"/*.h "$WORK/"

run_one() {
	name=$1
	shift

	rm -rf "$WORK/src"
	mkdir -p "$WORK/src"
	cp "$WORK"/*.c "$WORK"/*.h "$WORK/src/"

	"$@"

	if SRC="$WORK/src" OBJ="$WORK/obj" sh "$HERE/run.sh" \
	    >"$WORK/$name.log" 2>&1; then
		echo "SURVIVED  $name - the stand does not catch this"
		return 1
	fi

	echo "caught    $name: $(grep -m1 '^FAIL' "$WORK/$name.log" |
	    cut -c1-70)"
	return 0
}

mut_link() {
	sed -i 's/^\t\t    (r->cycle ? XHCI_TRB_C : 0);$/\t\t    0;/' \
	    "$WORK/src/xhci_ring.c"
	grep -q 'XHCI_TRB_TC |$' "$WORK/src/xhci_ring.c"
}

mut_first() {
	sed -i 's/type == want_type \&\& !got_wanted/type == want_type/' \
	    "$WORK/src/xhci_ring.c"
}

mut_isp() {
	sed -i 's/XHCI_TRB_TYPE(XHCI_TRB_DATA) | XHCI_TRB_ISP |/XHCI_TRB_TYPE(XHCI_TRB_DATA) |/' \
	    "$WORK/src/xhci_dev.c"
}

mut_doorbell() {
	sed -i 's|^\txhci_wr(xhci.regs, xhci.dboff + XHCI_DB(dev->slot), ep->dci);$|\t/* the doorbell, deliberately not rung */|' \
	    "$WORK/src/xhci_dev.c"
}

bad=0
for m in link first isp doorbell; do
	run_one "$m" "mut_$m" || bad=$((bad + 1))
done

echo
if [ "$bad" -eq 0 ]; then
	echo "all four defects are caught"
else
	echo "$bad defect(s) survived the stand"
fi
exit "$bad"
