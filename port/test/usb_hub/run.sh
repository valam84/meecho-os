#!/bin/sh
#
# Build and run the usb_hub host stand (hubtest.c): the hub driver, its own
# files unchanged, against a model of xhci's URB protocol, of DDEKit's
# cooperative threads and of the hubs on the wire.
#
#   sh run.sh          the checks, quiet
#   sh run.sh -v       every check and the driver's own words
#
# HERE is this directory, SRC the driver's; both can be overridden, which
# is how mutate.sh points the stand at a mutated copy.

set -e

HERE=${HERE:-$(cd "$(dirname "$0")" && pwd)}
TREE=$(cd "$HERE/../../.." && pwd)
SRC=${SRC:-$TREE/minix/drivers/usb/usb_hub}
OBJ=${OBJ:-${TMPDIR:-/tmp}/usbhubtest}

if [ ! -f "$SRC/usb_hub.c" ]; then
	echo "the driver is not at $SRC; set SRC" >&2
	exit 1
fi

mkdir -p "$OBJ"
CC=${CC:-gcc}
CFLAGS="-std=gnu99 -O1 -g -Wall -Wno-unused-parameter -Wno-unused-function"
# The port array is the last field of its structure, and GCC takes a
# trailing array for a flexible one unless told otherwise: no bounds check.
CFLAGS="$CFLAGS -fsanitize=undefined -fno-sanitize-recover=all"
CFLAGS="$CFLAGS -fstrict-flex-arrays=3"
CFLAGS="$CFLAGS -I$HERE/stubs -I$SRC -I$TREE/minix/include"

$CC $CFLAGS "$HERE/hubtest.c" -o "$OBJ/hubtest"

"$OBJ/hubtest" "$@"
