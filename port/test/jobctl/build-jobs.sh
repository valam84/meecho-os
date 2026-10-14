#!/bin/bash
# Build what port/test/jobctl/qemu-jobs.py boots and runs: interactive job
# control - process groups in PM, the foreground group in tty and pty, ash
# with JOBS.
#
#	build-jobs.sh [what...]
#
# Everything goes into ~/obj-jobs and a sysroot of its own, ~/dest-jobs,
# because the tree, ~/obj-evbarm64 and ~/dest-evbarm64 are shared by
# several sessions: the headers this work adds to and the libc and libsys
# it changes must not reach a program someone else links meanwhile.  The
# sysroot is a copy of DESTDIR's headers with this tree's on top, and
# DESTDIR's libraries as links, except the ones built here.
#
# 'what' is any of: sysroot libsys libc pm mib tty pty sh tests; none is all
# of them, in that order.  The boot images (pm, mib, tty) are loaded with
# BOOT_IMAGES=; pty, libc.so, sh and the tests reach the guest over HTTP from
# ~/obj-evbarm64/work-jobs/cc.  (is, which reads PM's table too, is not
# built for aarch64 at all.)  'shared' alone installs it all for everybody;
# see shared() below.
#
# The status is checked here and not in a wsl.exe command line, where $?
# reads zero whatever happened (CLAUDE.md).
set -uo pipefail
SRC=${SRCDIR:-/home/minix/minix-src}
DESTDIR=/home/minix/dest-evbarm64
SYSROOT=/home/minix/dest-jobs
VOBJ=/home/minix/obj-jobs
WRAP=/home/minix/tools-evbarm64/bin/nbmake-evbarm64-el
CC=/home/minix/xtools-aarch64/bin/aarch64-elf64-minix-gcc
OUT=/home/minix/obj-evbarm64/work-jobs/cc
MAKE=$VOBJ/nbmake
mkdir -p "$VOBJ" "$OUT" || exit 1
log=$VOBJ/build-jobs.log
: > "$log"
cd "$SRC" || exit 1

# The cross compiler has DESTDIR built in as its sysroot, so a DESTDIR of
# our own is not enough: the compiler drivers get --sysroot from a toolchain
# directory of links, and everything else in it is the real thing.
XT=/home/minix/xtools-aarch64
VXT=$VOBJ/xtools
mkdir -p $VXT/bin || exit 1
for f in $XT/*; do
	[ "$(basename $f)" = bin ] || ln -sfn $f $VXT/$(basename $f)
done
for f in $XT/bin/*; do
	b=$(basename $f)
	case $b in
	*-gcc|*-cpp|*-c++|*-g++)
		printf '#!/bin/sh\nexec %s --sysroot=%s "$@"\n' "$f" "$SYSROOT" \
		    > $VXT/bin/$b && chmod +x $VXT/bin/$b || exit 1 ;;
	*)	ln -sfn $f $VXT/bin/$b ;;
	esac
done

sed -e "s|^MAKEOBJDIR=.*|MAKEOBJDIR='\${.CURDIR:C,^$SRC,$VOBJ,}'; export MAKEOBJDIR|" \
    -e "s|^DESTDIR=.*|DESTDIR=$SYSROOT; export DESTDIR|" \
    -e "s|^EXTERNAL_TOOLCHAIN=.*|EXTERNAL_TOOLCHAIN=$VXT; export EXTERNAL_TOOLCHAIN|" \
    "$WRAP" > "$MAKE" && chmod +x "$MAKE" || exit 1
grep -q "obj-jobs,}" "$MAKE" && grep -q "^DESTDIR=$SYSROOT" "$MAKE" &&
    grep -q "^EXTERNAL_TOOLCHAIN=$VXT" "$MAKE" ||
	{ echo "FAIL: wrapper not redirected"; exit 1; }

fail()
{
	echo "FAIL $1 ($log)"
	grep -m8 -E "error:|Error|undefined reference" "$log" | sed "s|$SRC/||" |
	    cut -c1-200
	exit 1
}

# The headers this work changes, from the tree into the sysroot.
HEADERS="minix/include/minix/callnr.h:minix/callnr.h
minix/include/minix/ipc.h:minix/ipc.h
minix/include/minix/syslib.h:minix/syslib.h
sys/sys/wait.h:sys/wait.h"

sysroot()
{
	mkdir -p $SYSROOT/usr/lib || return 1
	rsync -a --delete $DESTDIR/usr/include/ $SYSROOT/usr/include/ || return 1
	for h in $HEADERS; do
		install -m 444 "${h%%:*}" "$SYSROOT/usr/include/${h#*:}" ||
		    return 1
	done
	# Everything else of DESTDIR by link, refreshed: a library rebuilt
	# there meanwhile is seen here.  The ones built here stay files.
	for f in $DESTDIR/*; do
		b=$(basename $f)
		[ "$b" = usr ] && continue
		[ -e $SYSROOT/$b ] || ln -s $f $SYSROOT/$b
	done
	for f in $DESTDIR/usr/*; do
		b=$(basename $f)
		case $b in include|lib) continue ;; esac
		[ -e $SYSROOT/usr/$b ] || ln -s $f $SYSROOT/usr/$b
	done
	for f in $DESTDIR/usr/lib/*; do
		b=$(basename $f)
		[ -f $SYSROOT/usr/lib/$b ] && [ ! -L $SYSROOT/usr/lib/$b ] &&
		    continue
		# A relative link is one of ours (libc.so -> libc.so.12.N).
		[ -L $SYSROOT/usr/lib/$b ] &&
		    [ "$(readlink $SYSROOT/usr/lib/$b | cut -c1)" != / ] &&
		    continue
		ln -sfn $f $SYSROOT/usr/lib/$b
	done
	echo "ok $SYSROOT"
}

# dir [library-to-put-into-the-sysroot]
build()
{
	local d=$1 lib=${2:-}
	{ $MAKE -C $d obj && $MAKE -C $d -j24 dependall; } >> "$log" 2>&1 ||
	    fail $d
	if [ -n "$lib" ]; then
		rm -f $SYSROOT/usr/lib/$lib
		cp $VOBJ/$d/$lib $SYSROOT/usr/lib/$lib || fail "$d $lib"
		echo "ok $SYSROOT/usr/lib/$lib"
	else
		echo "ok $VOBJ/$d"
	fi
}

prog()
{
	cp "$VOBJ/$1" "$OUT/$2" || fail "$1"
	echo "ok $OUT/$2"
}

# 'shared': once it is committed, into what the other sessions build against
# and boot - the headers, libsys and libc into DESTDIR, PM, mib and tty into
# ~/obj-evbarm64 (boot images), pty, ash and the tests into DESTDIR.  mib
# goes with PM: it reads PM's table.
shared()
{
	local d
	for h in $HEADERS; do
		install -m 444 "${h%%:*}" "$DESTDIR/usr/include/${h#*:}" ||
		    fail "install ${h%%:*}"
	done
	echo "ok headers into $DESTDIR"
	rm -f /home/minix/obj-evbarm64/bin/sh/sh
	for d in minix/lib/libsys lib/libc minix/servers/pm minix/servers/mib \
	    minix/drivers/tty/tty minix/drivers/tty/pty bin/sh; do
		{ $WRAP -C $d obj && $WRAP -C $d -j24 dependall &&
		  $WRAP -C $d install; } >> "$log" 2>&1 || fail "shared $d"
		echo "ok $d (shared)"
	done
	rm -f /home/minix/obj-evbarm64/minix/tests/test95 \
	    /home/minix/obj-evbarm64/minix/tests/test96
	{ $WRAP -C minix/tests obj && $WRAP -C minix/tests test95 test96; } \
	    >> "$log" 2>&1 || fail "shared minix/tests"
	for f in test95 test96; do
		install -m 755 /home/minix/obj-evbarm64/minix/tests/$f \
		    $DESTDIR/usr/tests/minix-posix/$f || fail "install $f"
	done
	install -m 755 minix/tests/run $DESTDIR/usr/tests/minix-posix/run ||
	    fail "install run"
	echo "ok test95 test96 run into $DESTDIR/usr/tests/minix-posix"
}

what=${*:-sysroot libsys libc pm mib tty pty sh tests}
for w in $what; do
	case $w in
	sysroot)	sysroot || fail sysroot ;;
	shared)		shared ;;
	libsys)		build minix/lib/libsys libsys.a ;;
	libc)		build lib/libc libc.a
			# The tests link dynamically, and run with the
			# guest's /usr/lib/libc.so: this one goes there too.
			for f in $VOBJ/lib/libc/libc.so.*.*; do
				case $f in *.map) continue ;; esac
				b=$(basename $f)
				rm -f $SYSROOT/usr/lib/$b
				cp $f $SYSROOT/usr/lib/$b || fail libc.so
				ln -sfn $b $SYSROOT/usr/lib/libc.so
				ln -sfn $b $SYSROOT/usr/lib/${b%.*}
				cp $f $OUT/$b || fail libc.so
				echo "ok $OUT/$b"
			done ;;
	pm)		build minix/servers/pm ;;
	mib)		build minix/servers/mib ;;
	tty)		build minix/drivers/tty/tty ;;
	pty)		build minix/drivers/tty/pty
			prog minix/drivers/tty/pty/pty pty ;;
	sh)		# libc.a is not among a program's dependencies: a
			# new libc relinks nothing unless the program goes.
			rm -f $VOBJ/bin/sh/sh
			build bin/sh; prog bin/sh/sh sh ;;
	tests)
		rm -f $VOBJ/minix/tests/test95 $VOBJ/minix/tests/test96
		{ $MAKE -C minix/tests obj &&
		  $MAKE -C minix/tests test95 test96; } >> "$log" 2>&1 ||
		    fail "minix/tests"
		prog minix/tests/test95 test95
		prog minix/tests/test96 test96
		$CC -O2 -static --sysroot=$SYSROOT -Wall -o "$OUT/jobprobe" \
		    port/test/jobctl/jobprobe.c >> "$log" 2>&1 ||
		    fail jobprobe
		echo "ok $OUT/jobprobe"
		;;
	*)	echo "unknown: $w"; exit 1 ;;
	esac
done
grep -E "warning:" "$log" | grep -v "^ld" | sed "s|$SRC/||" | cut -c1-200 | head
exit 0
