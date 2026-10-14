#!/bin/bash
# Build a variant of the kernel in an object directory of its own:
#
#	kbuild-variant.sh NAME "FLAGS"
#
# e.g.  kbuild-variant.sh ktrace "-O2 -DKTRACE=1"
#       kbuild-variant.sh noflow "-O2 -DIPCFLOW=0"
#
# FLAGS go in DBG, which sys.mk adds to CFLAGS - so they carry the
# optimisation level too.  The kernel lands in ~/obj-kvar/NAME/minix/kernel/
# kernel; ramimage.sh boots it with KERNEL_ELF= that path.
#
# Why not edit debug.h and rebuild: the tree and ~/obj-evbarm64 are shared
# by several sessions at once, and the kernel there is the one all of them
# boot.  debug.h takes KTRACE and IPCFLOW from the command line for this.
#
# The status is checked here and not in a wsl.exe command line, where $?
# reads zero whatever happened (CLAUDE.md).
set -uo pipefail
NAME=${1:?name}
FLAGS=${2:?flags, e.g. "-O2 -DKTRACE=1"}
SRC=${SRCDIR:-/home/minix/minix-src}
VOBJ=$HOME/obj-kvar/$NAME
WRAP=$HOME/tools-evbarm64/bin/nbmake-evbarm64-el
MAKE=$VOBJ/nbmake
log=$VOBJ/build.log

mkdir -p "$VOBJ" || exit 1
# The wrapper fixes MAKEOBJDIR to the shared tree; a copy of it points it
# here instead.  Libraries still come from DESTDIR, as for any kernel.
sed "s|^MAKEOBJDIR=.*|MAKEOBJDIR='\${.CURDIR:C,^$SRC,$VOBJ,}'; export MAKEOBJDIR|" \
    "$WRAP" > "$MAKE" && chmod +x "$MAKE" || exit 1
grep -q "obj-kvar/$NAME" "$MAKE" || { echo "FAIL: wrapper not redirected"; exit 1; }

cd "$SRC" || exit 1
{ $MAKE -C minix/kernel obj && $MAKE -C minix/kernel -j24 DBG="$FLAGS" dependall; } > "$log" 2>&1
if [ $? -ne 0 ]; then
	echo "FAIL kernel $NAME ($log)"
	grep -m8 -E "error:|Error|undefined reference" "$log" | sed "s|$SRC/||" | cut -c1-200
	exit 1
fi
K=$VOBJ/minix/kernel/kernel
[ -f "$K" ] || { echo "FAIL: no $K"; exit 1; }
echo "ok $K"
