#!/bin/bash
# Rebuild the kernel and the libraries linked into it with -fstack-usage and
# -fcallgraph-info=su. Neither changes code generation; GCC writes a .su
# (frame size per function) and a .ci (call graph, VCG) next to each object.
# Used by port/stack-depth.py (KERNEL-AUDIT #9).
#
# MK and OBJROOT move it out of the shared object directory, which it would
# otherwise cleandir under everybody else's feet: build a variant first
# (port/kbuild-variant.sh stack "-O2") and use its wrapper -
#   MK=~/obj-kvar/stack/nbmake OBJROOT=~/obj-kvar/stack stack-build.sh
#   stack-depth.py ~/obj-kvar/stack/minix
cd /home/minix/minix-src || exit 1
MK=${MK:-/home/minix/tools-evbarm64/bin/nbmake-evbarm64-el}
OBJROOT=${OBJROOT:-/home/minix/obj-evbarm64}
FL="-fstack-usage -fcallgraph-info=su"
for d in ${DIRS:-minix/lib/libminc minix/lib/libsys minix/lib/libtimers minix/lib/libexec minix/kernel}; do
	$MK -C $d cleandir >/dev/null 2>&1
	$MK -C $d obj >/dev/null 2>&1
	if ! $MK -C $d -j24 MKPIC=no MKPROFILE=no CPUFLAGS="$FL" dependall > /home/minix/stack-build.log 2>&1; then
		echo "FAILED: $d"; grep -m5 -E "error|Error" /home/minix/stack-build.log; exit 1
	fi
	echo "$d: $(find $OBJROOT/$d -name '*.ci' | wc -l) call graphs"
done
