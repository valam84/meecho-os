#!/bin/sh
#
# Build and run the xHCI host stand.
#
# It compiles the driver's own ring and device files - not copies of them -
# against the shim headers here and the model of the controller.  Nothing
# in the driver is changed for this: if a check fails, the driver is what
# is wrong.
#
#   sh run.sh          the checks, quiet
#   sh run.sh -v       every check and the driver's own logging
#
# HERE is this directory; SRC is the driver's.  Both can be overridden,
# which is what the CI-less version of this project has instead of one.

set -e

HERE=${HERE:-$(cd "$(dirname "$0")" && pwd)}
SRC=${SRC:-$HOME/minix-src/minix/drivers/usb/xhci}
OBJ=${OBJ:-/tmp/xhcitest}

if [ ! -f "$SRC/xhci_ring.c" ]; then
	echo "the driver is not at $SRC; set SRC" >&2
	exit 1
fi

mkdir -p "$OBJ"

CC=${CC:-gcc}
CFLAGS="-O1 -g -Wall -Wno-unused-parameter -Wno-unused-but-set-variable"
CFLAGS="$CFLAGS -I$HERE/shim -I$HERE -I$SRC -D_GNU_SOURCE"

for f in "$SRC/xhci_ring.c" "$SRC/xhci_dev.c" "$HERE/model.c" \
    "$HERE/ringtest.c"; do
	o="$OBJ/$(basename "$f" .c).o"
	$CC $CFLAGS -c "$f" -o "$o"
done

$CC "$OBJ"/*.o -o "$OBJ/ringtest"

"$OBJ/ringtest" "$@"
