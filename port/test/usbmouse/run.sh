#!/bin/sh
#
# Build and run the USB mouse report stand: the driver's own report.c,
# compiled by the host, against reporttest.c.
#
#   sh run.sh            the checks
#   sh run.sh -m         and then put each mutation in, one at a time;
#                        every one must be caught
#
# SRC is the driver's directory and can be overridden.

set -e

HERE=${HERE:-$(cd "$(dirname "$0")" && pwd)}
SRC=${SRC:-$HOME/minix-src/minix/drivers/usb/usb_mouse}
OBJ=${OBJ:-/tmp/usbmousetest}
CC=${CC:-gcc}

build_run() {
	mkdir -p "$OBJ"
	$CC -O1 -g -Wall -Wextra -Wno-unused-parameter -I"$1" \
	    "$1/report.c" "$HERE/reporttest.c" -o "$OBJ/reporttest"
	"$OBJ/reporttest"
}

build_run "$SRC"
[ "${1:-}" = "-m" ] || exit 0

#
# The mutations: the ways the arithmetic could be wrong, each of which
# would show on the screen as a cursor that does something else.
#
#   sign      the motion taken unsigned: left is a long way right
#   order     buttons before motion: a click lands where the move began
#   release   presses before releases: two buttons down that never were
#   padding   all eight bits taken as buttons
#   zero      a zero motion sent as an event
#   short     a report of two bytes taken as one
#   wheel     the wheel read whether it is there or not
#
mutate() {
	name=$1
	expr=$2
	rm -rf "$OBJ/m"
	mkdir -p "$OBJ/m"
	cp "$SRC/report.c" "$SRC/report.h" "$OBJ/m/"
	perl -0pi -e "$expr" "$OBJ/m/report.c"
	if cmp -s "$SRC/report.c" "$OBJ/m/report.c"; then
		echo "the $name mutation changed nothing" >&2
		exit 2
	fi
	if build_run "$OBJ/m" >"$OBJ/$name.log" 2>&1; then
		echo "SURVIVED  $name"
		bad=$((bad + 1))
	else
		echo "caught    $name: $(grep -m1 '^FAIL' "$OBJ/$name.log" |
		    cut -c1-60)"
	fi
}

bad=0
set +e
mutate sign 's/return b < 0x80 \? \(int\)b : \(int\)b - 0x100;/return (int)b;/'
mutate order 's/(\tif \(\(v = sbyte\(rep\[1\]\)\).*?\n)\n(\tnow = [^\n]*\n\n\tfor [^}]*\}\n\tfor [^}]*\}\n)/$2$1/s'
mutate release 's/(\tfor \(i = 0; i < MOUSE_BUTTONS[^}]*\}\n)(\tfor \(i = 0; i < MOUSE_BUTTONS[^}]*\}\n)/$2$1/s'
mutate padding 's/now = rep\[0\] & \(\(1u << MOUSE_BUTTONS\) - 1\);/now = rep[0];/'
mutate zero 's/if \(\(v = sbyte\(rep\[1\]\)\) != 0\)/v = sbyte(rep[1]);/'
mutate short 's/if \(len < MOUSE_REPORT_MIN\)\n\t\treturn old;\n//'
mutate wheel 's/wheel && len > MOUSE_REPORT_MIN && /wheel \&\& /'

echo
if [ "$bad" -eq 0 ]; then
	echo "every mutation is caught"
else
	echo "$bad mutation(s) survived"
fi
exit "$bad"
