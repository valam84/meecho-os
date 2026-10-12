#!/bin/sh
#
# Build and run the USB keyboard report stand: the driver's own report.c,
# compiled by the host, against reporttest.c.
#
#   sh run.sh            the checks
#   sh run.sh -m         and then put each mutation in, one at a time;
#                        every one must be caught
#
# SRC is the driver's directory and can be overridden.

set -e

HERE=${HERE:-$(cd "$(dirname "$0")" && pwd)}
SRC=${SRC:-$HOME/minix-src/minix/drivers/usb/usb_kbd}
OBJ=${OBJ:-/tmp/usbkbdtest}
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
# would show at the console as something other than a wrong letter.
#
#   order     presses before releases: "b up, c down" shows both held
#   modorder  modifiers after keys on the way down: shift+a typed as "a"
#   rollover  the rollover report taken at its word: every key released
#   slot      a key that moved slot taken for a release and a press
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
mutate order 's/(\tfor \(i = 2; i < KBD_REPORT_LEN; i\+\+\)\n\t\tif \(old\[i\] >= KBD_KEY_FIRST[^\n]*\n[^\n]*\n)(.*?)(\n\treturn 1;)/$2$1$3/s'
mutate modorder 's/(\tfor \(i = 0; i < 8; i\+\+\) \{\n\t\tbit = 1u << i;\n\t\tif \(!\(old\[0\][^}]*\}\n)(\n\tfor \(i = 2; i < KBD_REPORT_LEN; i\+\+\)\n\t\tif \(now\[i\][^\n]*\n[^\n]*\n)/$2$1/s'
mutate rollover 's/\t\tif \(now\[i\] == KBD_KEY_ROLLOVER\)\n\t\t\treturn 0;\n//'
mutate slot 's/!held_in\(now, old\[i\]\)/1/'

echo
if [ "$bad" -eq 0 ]; then
	echo "every mutation is caught"
else
	echo "$bad mutation(s) survived"
fi
exit "$bad"
