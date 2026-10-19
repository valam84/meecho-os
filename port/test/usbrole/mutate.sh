#!/bin/sh
# Does the usbrole stand notice when usbrole.sh is wrong?  Each mutation is
# one edit to a copy of the script; run.sh on the copy has to fail.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
TREE=$(cd "$HERE/../../.." && pwd)
SRC=$TREE/minix/usr.sbin/usbrole/usbrole.sh
W=${TMPDIR:-/tmp}/usbrole-mutate
mkdir -p "$W"
caught=0 missed=0

mutate() {	# name sed-expression
	sed "$2" "$SRC" > "$W/usbrole.sh"
	if cmp -s "$SRC" "$W/usbrole.sh"; then
		echo "BAD   $1: the mutation changed nothing"
		missed=$((missed + 1))
		return
	fi
	if USBROLE="$W/usbrole.sh" TMPDIR="$W" sh "$HERE/run.sh" \
	    > "$W/$1.log" 2>&1; then
		echo "MISS  $1"
		missed=$((missed + 1))
	else
		echo "ok    $1 ($(grep -m1 FAIL "$W/$1.log" | sed 's/^ *//' |
		    cut -c1-60))"
		caught=$((caught + 1))
	fi
}

mutate "one sighting moves the port" 's/\[ $seen -ge 2 \]/[ $seen -ge 1 ]/'
mutate "the host up before the network goes" \
    's/	host)	\[ "$cur" = device \] \&\& device_down/	host)	host_up; [ "$cur" = device ] \&\& device_down/'
mutate "no address after the driver comes back" \
    's/	ifconfig "$netif" inet "$NETADDR" up/	: "$netif"/'
mutate "the controller before its clients" \
    's/	for l in usb_mouse usb_kbd usb_hub usbd/	for l in usbd usb_mouse usb_kbd usb_hub/'
mutate "auto without dual role" 's/	if \[ "$POLICY" = auto \] \&\&/	if false \&\&/'
mutate "anything not a source is a host" \
    's/if \[ "$2" = sink \]/if [ "$2" != source ]/'
mutate "a request never runs out" 's/|| \[ "`now`" -lt "$w_until" \]; }/|| true; }/'
mutate "a daemon with usbrole=device" \
    's/	\[ "$POLICY" = device \] \&\& exit 0/	:/'

echo "mutate: $caught caught, $missed missed"
[ "$missed" -eq 0 ]
