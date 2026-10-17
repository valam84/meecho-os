#!/bin/sh
#
# usbrole(8): what a machine's one USB port is - a device to whoever is at
# the other end of the cable (the network to it), or a host to what is
# plugged in (a keyboard, a mouse, a hub) - decided by what the PMIC's
# Type-C block says is at the other end (sysctl minix.pmic.typec, the
# Mi Pad 4's qcom_pmic; port/clover/hw-usbhost.md, section 4).
#
#	usbrole			the role now, and what decided it
#	usbrole -1		decide once and bring that role up (rc)
#	usbrole -d		follow Type-C from now on (rc, in the background)
#	usbrole [-t N] host|device|auto
#				ask the daemon for a role - for N seconds, or
#				until asked again; "auto" follows Type-C again
#
# A sink at the other end (Rd: an OTG adapter, a device) makes the port a
# host; anything else - a host or a charger (Rp), nothing, a PMIC that does
# not answer - leaves it a device, which is what it always was.  The two
# roles drive the same controller, so the one is taken down whole before
# the other comes up: device - the network driver (sysenv netdrv, label
# <netdrv>_0) and its address (sysenv netaddr); host - xhci (label usbd),
# usb_hub, usb_kbd, usb_mouse.  lwip and sshd are rc's and stay.
#
# The boot parameter usbrole= is the policy: auto (the default), device
# (this does nothing - the port stays as rc made it), or host.  With auto
# the PMIC is told to let the port be a source (sysctl minix.pmic.otg=auto):
# with the boot loader's "sink only" Type-C never sees an adapter at all.
# (The three USBROLE_ variables are the host stand's, port/test/usbrole.)
PATH=${USBROLE_PATH:-/sbin:/usr/sbin:/bin:/usr/bin}
export PATH

RUN=${USBROLE_RUN:-/var/run}
WANT=$RUN/usbrole.want		# "role expiry": what was asked for
ROLE=$RUN/usbrole.role		# the role that is up now
PIDF=$RUN/usbrole.pid
SVC=${USBROLE_SVC:-/service}

say() {
	echo "usbrole: $*"
}

param() {	# name default: a boot parameter, or the default
	if v=`sysenv "$1" 2>/dev/null`
	then	echo "$v"
	else	echo "$2"
	fi
}

POLICY=`param usbrole auto`
NETDRV=`param netdrv dwc3_ecm`
NETADDR=`param netaddr 10.42.0.2/24`
NETLOG=`param netlog 2`
USBLOG=`param usblog 2`
KBDLOG=`param kbdlog 2`
MOUSELOG=`param mouselog 2`

typec() {
	sysctl -n minix.pmic.typec 2>/dev/null || echo unknown
}

vbus() {
	sysctl -n minix.pmic.vbus 2>/dev/null || echo unknown
}

now() {
	date +%s
}

# What the port should be for this policy and this Type-C state.
decide() {	# policy typec
	case $1 in
	host)	echo host ;;
	device)	echo device ;;
	*)	if [ "$2" = sink ]
		then	echo host
		else	echo device
		fi ;;
	esac
}

# The policy in force: what was asked for, unless it has run out.
policy() {
	if [ -f "$WANT" ]
	then
		read want until < "$WANT"
		if [ -n "$want" ] &&
		   { [ "${until:-0}" = 0 ] || [ "`now`" -lt "$until" ]; }
		then
			echo "$want"
			return
		fi
	fi
	echo "$POLICY"
}

down() {	# label
	minix-service down "$1" >/dev/null 2>&1
}

host_down() {
	for l in usb_mouse usb_kbd usb_hub usbd
	do
		down $l
	done
}

host_up() {
	minix-service up $SVC/xhci -label usbd \
	    -args "instance=0 log=$USBLOG" || { say "no USB host controller"; return 1; }
	minix-service up $SVC/usb_hub -label usb_hub ||
	    say "WARNING: no USB hub driver"
	minix-service up $SVC/usb_kbd -label usb_kbd -args "log=$KBDLOG" ||
	    say "WARNING: no USB keyboard driver"
	if [ -x $SVC/usb_mouse ]
	then	minix-service up $SVC/usb_mouse -label usb_mouse \
		    -args "log=$MOUSELOG" || say "WARNING: no USB mouse driver"
	fi
	return 0
}

device_down() {
	down "${NETDRV}_0"
}

# The network driver, and its address once lwip has made its interface:
# lwip drops an interface whose driver went away (ndev_down()), so a
# driver that comes back gets a new one with no address.
device_up() {
	if [ ! -x "$SVC/$NETDRV" ]
	then
		say "no $SVC/$NETDRV"
		return 1
	fi
	minix-service up "$SVC/$NETDRV" -label "${NETDRV}_0" \
	    -args "instance=0 log=$NETLOG" || { say "$NETDRV did not start"; return 1; }
	netif=
	for t in 1 2 3 4 5 6 7 8 9 10
	do
		for i in `ifconfig -l 2>/dev/null`
		do
			[ "$i" != lo0 ] && netif=$i
		done
		[ -n "$netif" ] && break
		sleep 1
	done
	if [ -z "$netif" ]
	then
		say "no interface from $NETDRV"
		return 1
	fi
	ifconfig "$netif" inet "$NETADDR" up || say "WARNING: $netif has no address"
	return 0
}

# Take the role that is up down, bring the other one up, write it down.
become() {	# role why
	cur=`cat "$ROLE" 2>/dev/null`
	[ "$cur" = "$1" ] && return 0
	say "$1 ($2)"
	case $1 in
	host)	[ "$cur" = device ] && device_down
		host_up ;;
	device)	[ "$cur" = host ] && host_down
		device_up ;;
	esac
	echo "$1" > "$ROLE"
}

# With "auto" Type-C has to be able to see a sink at all: dual role.
otg_auto() {
	if [ "$POLICY" = auto ] &&
	   [ "`sysctl -n minix.pmic.otg 2>/dev/null`" = off ]
	then	sysctl -w minix.pmic.otg=auto >/dev/null 2>&1 ||
		    say "WARNING: the PMIC would not let the port be a source"
	fi
}

case ${1:-} in
"")
	echo "role `cat "$ROLE" 2>/dev/null || echo none`, policy `policy`," \
	    "type-c `typec`, vbus `vbus`"
	exit 0
	;;
-1)
	mkdir -p "$RUN"
	rm -f "$WANT" "$ROLE"
	if [ "$POLICY" = device ]
	then
		become device "usbrole=device"
		exit 0
	fi
	otg_auto
	# The PMIC debounces its CC pins; give a just-attached adapter a look.
	t=`typec`
	if [ "$t" = unknown ] || [ "$t" = none ]
	then
		sleep 1
		t=`typec`
	fi
	become "`decide "$POLICY" "$t"`" "type-c $t"
	exit 0
	;;
-d)
	[ "$POLICY" = device ] && exit 0
	mkdir -p "$RUN"
	echo $$ > "$PIDF"
	trap '' HUP
	otg_auto
	last= seen=0
	while :
	do
		t=$(typec)
		want=$(decide "$(policy)" "$t")
		# Twice in a row before anything moves: the CC pins settle,
		# and a cable is not plugged in all at once.
		if [ "$want" = "$last" ]
		then	seen=$((seen + 1))
		else	seen=1
		fi
		last=$want
		[ $seen -ge 2 ] && become "$want" "type-c $t, policy $(policy)"
		sleep 1
	done
	;;
-t)
	[ $# -eq 3 ] || { echo "usage: usbrole [-t seconds] host|device|auto" >&2; exit 2; }
	until=$((`now` + $2))
	role=$3
	;;
host|device|auto)
	until=0
	role=$1
	;;
*)
	echo "usage: usbrole [-1 | -d | [-t seconds] host|device|auto]" >&2
	exit 2
	;;
esac

case $role in
host|device|auto) ;;
*)	echo "usage: usbrole [-t seconds] host|device|auto" >&2; exit 2 ;;
esac
if ! { [ -f "$PIDF" ] && kill -0 "`cat "$PIDF"`" 2>/dev/null; }
then
	echo "usbrole: no daemon follows the port (usbrole -d)" >&2
	exit 1
fi
echo "$role $until" > "$WANT.new" && mv "$WANT.new" "$WANT"
exit 0
