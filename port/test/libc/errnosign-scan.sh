#!/bin/sh
# Which libc source says "errno = E..." or "errno == E..." in a file built
# with _SYSTEM?  There <errno.h> makes every E* negative (the servers'
# convention) while errno in a process is positive, so the assignment stores
# a negative number and the comparison is never true (mmap.c, stage 11.6;
# sem.c and shm*.c, the SysV IPC calls).  Host-only, no build:
#
#   sh port/test/libc/errnosign-scan.sh [tree]      (default: ~/minix-src)
#
# Prints the offending lines; exit status 1 if there are any.  What it does
# not see: a file that gets _SYSTEM from a header (only minix/driver.h and
# drivers.h define it, and no libc file includes them) or from a Makefile
# (-D_SYSTEM; none in libc or libminc - it checks).

tree=${1:-$HOME/minix-src}
cd "$tree" || exit 2

dirs="minix/lib/libc lib/libc minix/lib/libminc lib/libminc"
bad=0

if grep -rnE '(^|[[:space:]])-D_(MINIX_)?SYSTEM' $dirs 2>/dev/null \
    --include=Makefile --include='Makefile.*' --include='*.mk' | grep .; then
	echo "errnosign-scan: a Makefile defines _SYSTEM for libc/libminc; extend this scan"
	exit 2
fi

for f in $(grep -rlE '^[[:space:]]*#[[:space:]]*define[[:space:]]+_SYSTEM\b' \
    $dirs --include='*.c' 2>/dev/null | sort); do
	hits=$(grep -nE 'errno[[:space:]]*(=[^=]|==|!=)[[:space:]]*-?E[A-Z0-9]+|E[A-Z0-9]+[[:space:]]*(==|!=)[[:space:]]*errno' "$f" |
	    grep -v '^[0-9]*:[[:space:]]*\*' | grep -v '^[0-9]*:[[:space:]]*/\*')
	if [ -n "$hits" ]; then
		echo "$f (built with _SYSTEM):"
		echo "$hits" | sed 's/^/    /'
		bad=1
	fi
done

if [ $bad -eq 0 ]; then
	echo "errnosign-scan: no libc file built with _SYSTEM assigns or compares errno with an E* constant"
fi
exit $bad
