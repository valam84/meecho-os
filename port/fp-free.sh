#!/bin/bash
# fp-free.sh [BINARY...] - do the system services touch FP/SIMD registers?
#
# They must not (share/mk/minix.service.mk): the kernel switches FP lazily,
# so a service that executes one FP instruction takes a trap on most context
# switches, and libminc's getcontext() leaves d8-d15 alone on the strength of
# this (minix/lib/libc/arch/aarch64/sys/ucontext.S) - a service that kept a
# value there across a libmthread switch would get another thread's.
#
# Without arguments: every service in DESTDIR/service and DESTDIR/sbin that
# links libminc's getcontext, plus the boot images in the object tree. Prints
# the first FP instructions of each offender; exit 1 if there is any.
# A service built with SERVICE_FP=yes is reported as well - it is allowed only
# if it does not use libmthread, which is what "uses getcontext" shows.
set -uo pipefail
DESTDIR=${DESTDIR:-$HOME/dest-evbarm64}
OBJ=${OBJ:-$HOME/obj-evbarm64}
SRC=${SRCDIR:-$HOME/minix-src}
OBJDUMP=${OBJDUMP:-$HOME/xtools-aarch64/bin/aarch64-elf64-minix-objdump}
NM=${NM:-$HOME/xtools-aarch64/bin/aarch64-elf64-minix-nm}

list=("$@")
if [ ${#list[@]} -eq 0 ]; then
	for f in "$DESTDIR"/service/*; do
		[ -f "$f" ] && list+=("$f")
	done
	# The boot images are not in DESTDIR/service: take them from the
	# object tree, where a service is the program named like its directory.
	for b in vm pm vfs rs ds sched mib tty memory mfs pfs init; do
		f=$(find "$OBJ/minix/servers" "$OBJ/minix/fs" "$OBJ/minix/drivers" \
		    -type f -name "$b" -perm -u+x 2>/dev/null | head -1)
		[ -n "$f" ] && list+=("$f")
	done
fi

bad=0 n=0
for f in "${list[@]}"; do
	# Libraries' and programs' own FP: any operand that is a b/h/s/d/q/v
	# register. Symbol names in <...> are cut first; they can look like one.
	hits=$("$OBJDUMP" -d --no-show-raw-insn "$f" 2>/dev/null |
	    sed 's/<[^>]*>//g; s|//.*||' |
	    grep -E '^ *[0-9a-f]+:' |
	    grep -E '[[:space:],{\[]([bhsdqv][0-9]{1,2})(\.[0-9]*[bhsdq])?([],}[:space:]]|$)')
	n=$((n + 1))
	[ -z "$hits" ] && continue
	bad=1
	mt=""
	"$NM" "$f" 2>/dev/null | grep -qw "_getcontext" && mt=" (uses getcontext)"
	echo "FP in $(basename "$f")$mt: $(echo "$hits" | wc -l) instructions"
	echo "$hits" | head -3 | sed 's/^/    /'
done
echo "$n checked"
exit $bad
