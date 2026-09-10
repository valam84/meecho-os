#!/bin/sh
#
# Does the stand actually catch anything?
#
# A set of checks that passes is worth nothing until it has been shown to
# fail.  This puts each of the driver's real defects back, one at a time,
# into a COPY of the driver, and runs the stand against it.  Every one of
# them must be caught; a mutation that survives is a check that is
# decoration.
#
# The defects are the real ones, in the words of the commits that fixed
# them:
#
#   link     the Link TRB handed to the controller without the cycle bit
#            it is looking for - 255 commands through, the next 45 silent
#   first    keeping the LAST event of the wanted kind rather than the
#            first - every short read reported as a full one
#   isp      no interrupt-on-short-packet on the data stage - the same
#            symptom, reached from the other end
#   doorbell a transfer queued and never announced - no event at all, and
#            nothing pending in the controller
#   ack      an event taken off the ring for a client and never
#            acknowledged, because the loop took a shortcut past
#            event_done().  The ring fills and the controller stops
#            writing events for everybody, which is what both clients
#            stalling on the board looked like.
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
	    cut -c1-68)"
	return 0
}

# Each mutation ends by checking that it changed something: a sed that
# quietly matches nothing would report the defect as caught when what was
# tested was the driver as it stands.
changed() {
	if cmp -s "$WORK/$1" "$WORK/src/$1"; then
		echo "the $2 mutation changed nothing" >&2
		exit 2
	fi
}

mut_link() {
	sed -i 's/^\t\t    (r->cycle ? XHCI_TRB_C : 0);$/\t\t    0;/' \
	    "$WORK/src/xhci_ring.c"
	changed xhci_ring.c link
}

mut_first() {
	perl -0pi -e 's/ \&\&\n\t\t\t    !got_wanted\) \{/) {/' \
	    "$WORK/src/xhci_ring.c"
	perl -0pi -e 's/type == want_type \&\& !got_wanted\) \{/type == want_type) {/' \
	    "$WORK/src/xhci_ring.c"
	changed xhci_ring.c first
}

mut_isp() {
	perl -0pi -e 's/XHCI_TRB_TYPE\(XHCI_TRB_DATA\) \| XHCI_TRB_ISP \|/XHCI_TRB_TYPE(XHCI_TRB_DATA) |/g' \
	    "$WORK/src/xhci_dev.c"
	changed xhci_dev.c isp
}

mut_doorbell() {
	perl -0pi -e 's/\n\txhci_wr\(xhci\.regs, xhci\.dboff \+ XHCI_DB\(dev->slot\), ep->dci\);\n/\n\t\/* the doorbell, deliberately not rung *\/\n/g' \
	    "$WORK/src/xhci_dev.c"
	changed xhci_dev.c doorbell
}

mut_ack() {
	perl -0pi -e 's/claimed = xhci_urb_transfer_event\(&ev\);/if (xhci_urb_transfer_event(&ev))\n\t\t\t\t\tcontinue;/' \
	    "$WORK/src/xhci_ring.c"
	changed xhci_ring.c ack
}

mut_wake() {
	perl -0pi -e 's/\tif \(event_waiting\(\)\)\n\t\treturn elapsed\(t_start\);\n//' \
	    "$WORK/src/xhci_ring.c"
	changed xhci_ring.c wake
}

bad=0
for m in link first isp doorbell ack wake; do
	run_one "$m" "mut_$m" || bad=$((bad + 1))
done

echo
if [ "$bad" -eq 0 ]; then
	echo "every defect is caught"
else
	echo "$bad defect(s) survived the stand"
fi
exit "$bad"
