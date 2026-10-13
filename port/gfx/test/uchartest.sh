#!/bin/sh
# compat/uchar.c against glibc in C.UTF-8, on the host (uchartest.c).
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
out=${TMPDIR:-/tmp}/uchartest.$$
mkdir -p "$out"
cc -O2 -Wall -c -Dmbrtoc32=my_mbrtoc32 -Dc32rtomb=my_c32rtomb \
    -Dmbrtoc16=my_mbrtoc16 -Dc16rtomb=my_c16rtomb \
    "$here/compat/uchar.c" -o "$out/uchar.o"
cc -O2 -Wall "$here/test/uchartest.c" "$out/uchar.o" -o "$out/uchartest"
"$out/uchartest"
rc=$?
rm -rf "$out"
exit $rc
