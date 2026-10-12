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
#   wake     acknowledging the line and sleeping without one more look
#            at the ring - an event that landed in between is slept
#            through until the deadline
#   td       matching a client's event against the last TRB of the
#            descriptor alone, so that a short answer is reported as a
#            full one - the same mistake as "first", made again in the
#            asynchronous path.  "Invalid descriptor length" and a
#            first open failing with EIO on every bring-up
#   ehb      an interrupt handler that never says it has finished when
#            it found nothing: the controller keeps its busy flag and
#            raises nothing more.  "the transfer had finished and nobody
#            was told", twice per bring-up, after every polled sequence
#   tdsize   TD Size left at zero on a chained entry - the controller ends
#            the transfer there, and the rest of the data answers the
#            NEXT request: "CSW tag mismatch" on every 64 KiB read
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

#
# td: the asynchronous path's own version of "first" - matching an event
#     against the last TRB of the descriptor alone, so that the event on a
#     short data stage, the one carrying how much arrived, is not
#     recognised and the status stage's "nothing left over" is believed.
#     The board: "Invalid descriptor length", and a first open that
#     failed with EIO on every bring-up.
#
mut_td() {
	perl -0pi -e 's/\t\tif \(\(uint32_t\)td->trb\[i\] == ev->p0\)\n\t\t\tbreak;/\t\tif (i == td->n - 1 \&\& (uint32_t)td->trb[i] == ev->p0)\n\t\t\tbreak;/' \
	    "$WORK/src/xhci_dev.c"
	changed xhci_dev.c td
}

#
# ehb: the interrupt handler that does not say it has finished.  The
#      controller sets Event Handler Busy when it raises the line and
#      clears it only on a write of the dequeue pointer; a driver that
#      polls can take an event before the line is raised for it, then
#      wake to an empty ring and go back to sleep without that write -
#      after which the controller writes events and raises nothing.
#      The board: "the transfer had finished and nobody was told", twice
#      per bring-up, each time the first transfer after enumeration.
#
mut_ehb() {
	perl -0pi -e 's/\t\(void\)xhci_events_drain\(0, NULL, 0\);\n\txhci_event_handled\(\);\n/\t(void)xhci_events_drain(0, NULL, 0);\n/' \
	    "$WORK/src/xhci_ring.c"
	perl -0pi -e 's/\t\t\t\txhci_irq_ack\(\);\n\t\t\t\txhci_event_handled\(\);\n/\t\t\t\txhci_irq_ack();\n/' \
	    "$WORK/src/xhci_ring.c"
	changed xhci_ring.c ehb
}

#
# tdsize: the packets-to-come field left at zero on a chained entry.  The
#         controller reads that as the end of the descriptor and finishes
#         the transfer there; the rest of the data arrives as the answer
#         to the next request.  The board: "CSW tag mismatch" on every
#         read of 64 KiB, the first size that crosses a 64 KiB boundary.
#
mut_tdsize() {
	perl -0pi -e 's/\(uint32_t\)chunk \| XHCI_TRB_TD_SIZE\(td_size\), control\);/(uint32_t)chunk, control);/' \
	    "$WORK/src/xhci_dev.c"
	changed xhci_dev.c tdsize
}

#
# The next five came with the keyboard (2026-10-12), and unlike the ones
# above they were not found on the board first: they are the ways the
# keyboard's code would be wrong if it were wrong, put in before the board
# could show them, because each is silent there.
#
# interval: bInterval written into the context as it stands.  At full and
#           low speed it is milliseconds and the context wants 2^n
#           microframes - a 10 ms keyboard polled every 128 ms.
#
mut_interval() {
	perl -0pi -e 's/XHCI_EP_INTERVAL\(ep_interval\(dev, ep\)\)/XHCI_EP_INTERVAL(ep->interval)/' \
	    "$WORK/src/xhci_dev.c"
	changed xhci_dev.c interval
}

#
# trdeq: an endpoint reset without moving the dequeue pointer.  The next
#        transfer is read into whatever the halt left on the ring, and the
#        one waiting for the report waits for ever.
#
mut_trdeq() {
	perl -0pi -e 's/XHCI_TRB_TYPE\(XHCI_TRB_SET_TR_DEQ\)/XHCI_TRB_TYPE(XHCI_TRB_NOOP_CMD)/' \
	    "$WORK/src/xhci_dev.c"
	changed xhci_dev.c trdeq
}

#
# ownbuf: an interrupt transfer into the device's buffer after all - the
#         one the next LED request goes through.
#
mut_ownbuf() {
	perl -0pi -e 's/phys_bytes phys = buf_phys, last = 0;/phys_bytes phys = dev->buf_phys, last = 0;/' \
	    "$WORK/src/xhci_dev.c"
	changed xhci_dev.c ownbuf
}

#
# ep0mps: a full-speed device left on 8-byte control packets.  One that
#         wants 64 answers eighteen bytes in one packet: babble.
#
mut_ep0mps() {
	perl -0pi -e 's/if \(dev->speed == 1 \&\& \(r = ep0_fix_max_packet/if (0 \&\& (r = ep0_fix_max_packet/' \
	    "$WORK/src/xhci_dev.c"
	changed xhci_dev.c ep0mps
}

#
# hubslot: a hub never described as one in its slot context; the split
#          transactions to a slow device behind it are scheduled by those
#          fields.
#
mut_hubslot() {
	perl -0pi -e 's/\tif \(dev->class == 9\)\n\t\thub_slot_update\(dev\);\n//' \
	    "$WORK/src/xhci_dev.c"
	changed xhci_dev.c hubslot
}

bad=0
for m in link first isp doorbell ack wake td ehb tdsize \
    interval trdeq ownbuf ep0mps hubslot; do
	run_one "$m" "mut_$m" || bad=$((bad + 1))
done

echo
if [ "$bad" -eq 0 ]; then
	echo "every defect is caught"
else
	echo "$bad defect(s) survived the stand"
fi
exit "$bad"
