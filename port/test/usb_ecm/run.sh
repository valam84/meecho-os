#!/bin/sh
#
# Build and run the usb_ecm host stand (ecmtest.c): the CDC Ethernet host
# driver, its own files unchanged, against a model of xhci's URB protocol,
# of two devices and of libnetdriver.
#
#   sh run.sh          the checks, quiet
#   sh run.sh -v       every check, the driver's log, xhci's complaints
#
# HERE is this directory, SRC the driver's; both can be overridden, which
# is how mutate.sh points the stand at a mutated copy.

set -e

HERE=${HERE:-$(cd "$(dirname "$0")" && pwd)}
TREE=$(cd "$HERE/../../.." && pwd)
SRC=${SRC:-$TREE/minix/drivers/usb/usb_ecm}
OBJ=${OBJ:-${TMPDIR:-/tmp}/usbecmtest}

if [ ! -f "$SRC/usb_ecm.c" ]; then
	echo "the driver is not at $SRC; set SRC" >&2
	exit 1
fi

mkdir -p "$OBJ"
CC=${CC:-gcc}
CFLAGS="-std=gnu99 -O1 -g -Wall -Wextra -Wno-unused-parameter"
CFLAGS="$CFLAGS -Wno-sign-compare -I$HERE/stubs -I$SRC"

$CC $CFLAGS -c "$SRC/ecm_desc.c" -o "$OBJ/ecm_desc.o"
$CC $CFLAGS -c "$HERE/ecmtest.c" -o "$OBJ/ecmtest.o"
$CC "$OBJ/ecm_desc.o" "$OBJ/ecmtest.o" -o "$OBJ/ecmtest"

"$OBJ/ecmtest" "$@"
