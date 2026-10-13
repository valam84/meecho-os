#!/bin/sh
# The strip's arithmetic on the host: striptest.c over meechowm's layout.c.
#
#   sh port/gfx/test/striptest.sh
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$HERE/../../../minix/usr.bin/meechowm
OUT=${TMPDIR:-/tmp}/striptest.$$
cc -O2 -Wall -Wextra -Werror -I"$SRC" -o "$OUT" "$HERE/striptest.c" \
    "$SRC/layout.c" || exit 1
"$OUT"
r=$?
rm -f "$OUT"
exit $r
