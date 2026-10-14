#!/bin/bash
# Build what port/test/jobctl/qemu-jobctl.py boots and runs:
#
#	build.sh          PM and mib in ~/obj-jobctl, an object directory of
#	                  their own (the stand boots them with BOOT_IMAGES=), so
#	                  that the shared ones stay what other sessions boot
#	build.sh shared   PM and mib in ~/obj-evbarm64
#
# and in either case <sys/wait.h> into DESTDIR, jobprobe and test95 into
# ~/obj-evbarm64/work-jobctl/cc.  test95 is also built by the tree's own
# minix/tests Makefile (in the same object directory as PM), as a check that
# it is wired in.
#
# The status is checked here and not in a wsl.exe command line, where $?
# reads zero whatever happened (CLAUDE.md).
set -uo pipefail
SRC=${SRCDIR:-/home/minix/minix-src}
DESTDIR=/home/minix/dest-evbarm64
WRAP=/home/minix/tools-evbarm64/bin/nbmake-evbarm64-el
CC=/home/minix/xtools-aarch64/bin/aarch64-elf64-minix-gcc
OUT=/home/minix/obj-evbarm64/work-jobctl/cc
if [ "${1:-}" = shared ]; then
	MAKE=$WRAP
	VOBJ=/home/minix/obj-evbarm64
else
	VOBJ=/home/minix/obj-jobctl
	mkdir -p "$VOBJ" || exit 1
	MAKE=$VOBJ/nbmake
	sed "s|^MAKEOBJDIR=.*|MAKEOBJDIR='\${.CURDIR:C,^$SRC,$VOBJ,}'; export MAKEOBJDIR|" \
	    "$WRAP" > "$MAKE" && chmod +x "$MAKE" || exit 1
	grep -q "obj-jobctl" "$MAKE" || { echo "FAIL: wrapper not redirected"; exit 1; }
fi
mkdir -p "$OUT" "$VOBJ" || exit 1
log=$VOBJ/jobctl-build.log
: > "$log"
cd "$SRC" || exit 1

# WCONTINUED, WIFCONTINUED: PM and the tests take them from DESTDIR.
install -m 444 sys/sys/wait.h $DESTDIR/usr/include/sys/wait.h || exit 1

for d in minix/servers/pm minix/servers/mib; do
	{ $MAKE -C $d obj && $MAKE -C $d -j24 dependall; } >> "$log" 2>&1
	if [ $? -ne 0 ]; then
		echo "FAIL $d ($log)"
		grep -m8 -E "error:|Error|undefined reference" "$log" | sed "s|$SRC/||" | cut -c1-200
		exit 1
	fi
	echo "ok $VOBJ/$d/$(basename $d)"
done

{ $MAKE -C minix/tests obj && $MAKE -C minix/tests test95; } >> "$log" 2>&1
if [ $? -ne 0 ] || [ ! -f "$VOBJ/minix/tests/test95" ]; then
	echo "FAIL minix/tests test95 ($log)"
	grep -m8 -E "error:|Error|undefined reference" "$log" | sed "s|$SRC/||" | cut -c1-200
	exit 1
fi
cp "$VOBJ/minix/tests/test95" "$OUT/test95" || exit 1
echo "ok $OUT/test95"

$CC -O2 -static --sysroot=$DESTDIR -Wall -o "$OUT/jobprobe" \
    port/test/jobctl/jobprobe.c >> "$log" 2>&1 || {
	echo "FAIL jobprobe ($log)"; tail -5 "$log"; exit 1; }
echo "ok $OUT/jobprobe"
grep -E "warning:" "$log" | sed "s|$SRC/||" | cut -c1-200 | head
exit 0
