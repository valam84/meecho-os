#!/bin/sh
# Host test of usbrole(8) (minix/usr.sbin/usbrole/usbrole.sh): the script
# itself, run by the host's sh against stand-ins for sysctl, sysenv,
# minix-service, ifconfig and sleep (fake.sh).  The stand-in sleep lets the
# daemon go round exactly as many times as a check says, so nothing here
# depends on timing except the one check of an expiry.
#
# Checked: the decision once at boot (-1) - the hub's cable, an OTG
# adapter, no PMIC, usbrole=device - and that "auto" lets the port be a
# source first; the daemon (-d): a change seen once moves nothing, seen
# twice moves the port, the old role's drivers all go before the new
# role's first comes up, never both at once, the address comes back with
# the network driver; a role asked for (and for how long); no daemon, no
# asking.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
TREE=$(cd "$HERE/../../.." && pwd)
USBROLE=${USBROLE:-$TREE/minix/usr.sbin/usbrole/usbrole.sh}
T=${TMPDIR:-/tmp}/usbrole-test
SH=${SH:-sh}

rm -rf "$T"
mkdir -p "$T/bin" "$T/service"
for c in sysenv sysctl minix-service ifconfig sleep
do
	ln -s "$HERE/fake.sh" "$T/bin/$c"
done
for s in xhci usb_hub usb_kbd usb_mouse dwc3_ecm
do
	: > "$T/service/$s"
	chmod +x "$T/service/$s"
done

STATE=$T/state
export STATE
export USBROLE_PATH="$T/bin:/bin:/usr/bin"
export USBROLE_RUN="$T/run"
export USBROLE_SVC="$T/service"

checks=0 failures=0
check() {	# what condition...
	what=$1
	shift
	checks=$((checks + 1))
	if ! "$@"
	then
		failures=$((failures + 1))
		echo "  FAIL: $what"
	fi
}

reset() {	# typec ("none" file: no PMIC)
	rm -rf "$STATE" "$USBROLE_RUN"
	mkdir -p "$STATE/env"
	[ "$1" != nopmic ] && echo "$1" > "$STATE/typec"
	echo off > "$STATE/otg"
	: > "$STATE/running"
	: > "$STATE/log"
	echo 1000000 > "$STATE/ticks"
	echo 0 > "$STATE/slept"
}

running() {	# label...: exactly these are up
	[ "$(sort "$STATE/running" | tr '\n' ' ')" = \
	    "$(printf '%s\n' "$@" | sort | tr '\n' ' ')" ]
}
logged() {	# line: it is in the log
	grep -qx "$1" "$STATE/log"
}
role() {
	[ "$(cat "$USBROLE_RUN/usbrole.role" 2>/dev/null)" = "$1" ]
}
before() {	# a b: line a is logged before line b
	a=$(grep -nx "$1" "$STATE/log" | head -1 | cut -d: -f1)
	b=$(grep -nx "$2" "$STATE/log" | tail -1 | cut -d: -f1)
	[ -n "$a" ] && [ -n "$b" ] && [ "$a" -lt "$b" ]
}
no_violation() {
	[ ! -s "$STATE/violation" ]
}

# The daemon is held in its sleep number s (slept = s, ticks = s - 1).
# Let it go round n times: sleeps s .. s+n-1 finish, and it is held again
# in sleep s+n.
step() {
	s=$(cat "$STATE/slept")
	echo $((s + $1 - 1)) > "$STATE/ticks"
	i=0
	while [ "$(cat "$STATE/slept")" -lt $((s + $1)) ] && [ $i -lt 500 ]
	do
		sleep 0.01
		i=$((i + 1))
	done
}

HOST_UP="usbd usb_hub usb_kbd usb_mouse"

echo "-- once at boot: the hub's cable"
reset source
$SH "$USBROLE" -1 > "$T/out" 2>&1
check "the network driver up, nothing else" running dwc3_ecm_0
check "its address" logged "addr ecm0 10.42.0.2/24"
check "the role written down" role device
check "auto lets the port be a source" logged "sysctl otg=auto"

echo "-- once at boot: an OTG adapter"
reset sink
$SH "$USBROLE" -1 > "$T/out" 2>&1
check "the host's drivers up, no network driver" running $HOST_UP
check "the role written down" role host

echo "-- once at boot: no PMIC driver"
reset nopmic
$SH "$USBROLE" -1 > "$T/out" 2>&1
check "a device, as always" running dwc3_ecm_0
check "the role written down" role device

echo "-- once at boot: usbrole=device"
reset sink
echo device > "$STATE/env/usbrole"
$SH "$USBROLE" -1 > "$T/out" 2>&1
check "a device whatever Type-C says" running dwc3_ecm_0
check "the PMIC left alone" sh -c "! grep -q sysctl '$STATE/log'"
timeout 5 $SH "$USBROLE" -d > "$T/out" 2>&1
check "and no daemon: -d leaves at once" test $? -ne 124
check "and no daemon: no pid file" sh -c "[ ! -f '$USBROLE_RUN/usbrole.pid' ]"

echo "-- the daemon: an adapter in, a flicker, the hub's cable back"
reset source
$SH "$USBROLE" -1 > "$T/out" 2>&1
echo 0 > "$STATE/ticks"
: > "$STATE/log"
$SH "$USBROLE" -d > "$T/daemon.out" 2>&1 &
DPID=$!
step 0
check "nothing moves while Type-C says the same" sh -c "[ ! -s '$STATE/log' ]"
echo sink > "$STATE/typec"
step 1
check "a sink seen once moves nothing" running dwc3_ecm_0
step 1
check "seen twice: the host" running $HOST_UP
check "the role written down" role host
check "the network driver went before the host's came" \
    before "down dwc3_ecm_0" "up usbd"
check "never both at once" no_violation
: > "$STATE/log"
echo source > "$STATE/typec"
step 1
echo sink > "$STATE/typec"
step 2
check "a one-second flicker moves nothing" sh -c "[ ! -s '$STATE/log' ]"
echo source > "$STATE/typec"
step 2
check "the hub's cable back: a device again" running dwc3_ecm_0
check "the clients went before the controller" before "down usb_mouse" "down usbd"
check "every host driver went before the network driver came" \
    before "down usbd" "up dwc3_ecm_0"
check "the address came back" logged "addr ecm0 10.42.0.2/24"
check "never both at once" no_violation

echo "-- asked for: host for a moment, then Type-C again"
$SH "$USBROLE" -t 2 host > "$T/out" 2>&1
check "the request is taken" test $? -eq 0
step 2
check "host while asked" running $HOST_UP
sleep 2.2
step 2
check "back to what Type-C says when it runs out" running dwc3_ecm_0
$SH "$USBROLE" host > "$T/out" 2>&1
step 2
check "host until asked otherwise" running $HOST_UP
$SH "$USBROLE" auto > "$T/out" 2>&1
step 2
check "auto: Type-C again" running dwc3_ecm_0
check "never both at once" no_violation

echo "-- the daemon with no PMIC answering"
rm -f "$STATE/typec"
step 3
check "a device stays a device" running dwc3_ecm_0

kill "$DPID" 2>/dev/null
echo 1000000 > "$STATE/ticks"
wait "$DPID" 2>/dev/null

echo "-- asking with no daemon"
rm -f "$USBROLE_RUN/usbrole.pid"
$SH "$USBROLE" host > "$T/out" 2>&1
check "refused" test $? -eq 1
check "and says why" grep -q "no daemon" "$T/out"

echo "usbrole stand: $checks checks, $failures failed"
[ "$failures" -eq 0 ]
