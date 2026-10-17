#!/bin/sh
#
# Does the usb_hub stand catch anything?
#
# Each mutation puts a defect into a COPY of the driver - the old driver's
# own where there was one, the way the new one would be wrong where there
# was not - and runs the stand against it.  Every one must be caught.
#
#   nolock    a hub's request made without the lock on talking to xhci:
#             two threads with requests out at once, and xhci's synchronous
#             completion of one taken by the other's sendrec for a reply
#   newslock  the same for news
#   whose     the completion not told whose request it was - the one
#             semaphore of the one-hub driver, now NULL
#   onehub    news naming the first hub whatever hub it is about
#   powerdown a port whose device fails powered off for good, as before:
#             a phone on the dock that stops charging
#   nooc      a port that drew too much never powered again
#   nogood    ports looked at before their power is good
#   resync    a device nobody was told about called a port error, as
#             before: what a restarted driver finds on every port in use
#   bounds    the port array sized to the limit and indexed from one, as
#             before - written one past its end by a hub of that many ports
#   stay      a withdrawn hub's thread going on polling it
#   release   the entry of a withdrawn hub never given back
#   restart   a restart refused, as before
#   noretry   a device the host controller could not address given up on at
#             once, as before - the dock's RTL8152 on the board
#
#   sh mutate.sh

set -e

HERE=${HERE:-$(cd "$(dirname "$0")" && pwd)}
TREE=$(cd "$HERE/../../.." && pwd)
SRC=${SRC:-$TREE/minix/drivers/usb/usb_hub}
WORK=${WORK:-${TMPDIR:-/tmp}/usbhubmutate}

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

	echo "caught    $name: $(grep -m1 -e '^FAIL' -e 'runtime error' \
	    "$WORK/$name.log" | cut -c1-68)"
	return 0
}

changed() {
	if cmp -s "$WORK/$1" "$WORK/src/$1"; then
		echo "the $2 mutation changed nothing" >&2
		exit 2
	fi
}

mut_nolock() {
	perl -0pi -e 's/\tddekit_sem_down\(bus\);\n\tr = blocking_urb_submit\(urb, s->done, URB_SUBMIT_CHECK_LEN\);\n\tddekit_sem_up\(bus\);\n/\tr = blocking_urb_submit(urb, s->done, URB_SUBMIT_CHECK_LEN);\n/' \
	    "$WORK/src/usb_hub.c"
	changed usb_hub.c nolock
}

mut_newslock() {
	perl -0pi -e 's/\tddekit_sem_down\(bus\);\n\tr = ddekit_usb_info\(s->dev, type, \(long\)port_num\);\n\tddekit_sem_up\(bus\);\n/\tr = ddekit_usb_info(s->dev, type, (long)port_num);\n/' \
	    "$WORK/src/usb_hub.c"
	changed usb_hub.c newslock
}

mut_whose() {
	perl -0pi -e 's/\turb->priv = sem;\n//' "$WORK/src/urb_helper.c"
	changed urb_helper.c whose
}

mut_onehub() {
	perl -0pi -e 's/r = ddekit_usb_info\(s->dev, type/r = ddekit_usb_info(hubs[0].dev, type/' \
	    "$WORK/src/usb_hub.c"
	changed usb_hub.c onehub
}

mut_powerdown() {
	perl -0pi -e 's/\t\t\t\t\t\t\tCLEAR_FEATURE,\n\t\t\t\t\t\t\tPORT_ENABLE\)\) \{/\t\t\t\t\t\t\tCLEAR_FEATURE,\n\t\t\t\t\t\t\tPORT_POWER)) {/' \
	    "$WORK/src/usb_hub.c"
	changed usb_hub.c powerdown
}

mut_nooc() {
	perl -0pi -e 's/\t\tif \(!status->PORT_OVER_CURRENT && !status->PORT_POWER\) \{/\t\tif (0) {/' \
	    "$WORK/src/usb_hub.c"
	changed usb_hub.c nooc
}

mut_nogood() {
	perl -0pi -e 's/\tddekit_thread_msleep\(wait\);\n//' "$WORK/src/usb_hub.c"
	changed usb_hub.c nogood
}

mut_resync() {
	perl -0pi -e 's/\t\t\t\tHUB_PMSG\(s, "Port%d: a device that nobody was "\n\t\t\t\t\t"told about", port_num\);\n/\t\t\t\treturn HUB_CHANGE_STATUS_ERR;\n/' \
	    "$WORK/src/usb_hub.c"
	changed usb_hub.c resync
}

mut_bounds() {
	perl -0pi -e 's/port_conn conn\[USB_HUB_PORT_LIMIT \+ 1\];/port_conn conn[USB_HUB_PORT_LIMIT];/' \
	    "$WORK/src/usb_hub.c"
	changed usb_hub.c bounds
}

mut_stay() {
	perl -0pi -e 's/\t\t\thubs\[i\]\.gone = 1;\n//' "$WORK/src/usb_hub.c"
	changed usb_hub.c stay
}

mut_release() {
	perl -0pi -e 's/\t\ts->dev = NULL;\n\t\ts->used = 0;\n/\t\ts->dev = NULL;\n/' \
	    "$WORK/src/usb_hub.c"
	changed usb_hub.c release
}

mut_noretry() {
	perl -0pi -e 's/\t\tif \(attempt >= USB_HUB_ENUM_TRIES\) \{/\t\tif (1) {/' \
	    "$WORK/src/usb_hub.c"
	changed usb_hub.c noretry
}

mut_restart() {
	perl -0pi -e 's/\t\tcase SEF_INIT_FRESH:\n\t\tcase SEF_INIT_RESTART:\n\t\t\treturn EXIT_SUCCESS;\n/\t\tcase SEF_INIT_FRESH:\n\t\t\treturn EXIT_SUCCESS;\n/' \
	    "$WORK/src/usb_hub.c"
	changed usb_hub.c restart
}

bad=0
for m in nolock newslock whose onehub powerdown nooc nogood resync bounds \
    stay release restart noretry; do
	run_one "$m" "mut_$m" || bad=$((bad + 1))
done

echo
if [ "$bad" -eq 0 ]; then
	echo "every defect is caught"
else
	echo "$bad defect(s) survived the stand"
fi
exit "$bad"
