#!/bin/bash
# Build MEECHO for AArch64 from a clean tree, in one command.
#
#	bash port/build-all.sh [stage ...]
#
# With no arguments it runs every stage in order.  Naming stages runs only
# those, which is what you want after touching one part:
#
#	tools     the NetBSD host tools (nbmake and friends)     ~15 min
#	xtools    the cross toolchain for the target triple      ~40 min
#	includes  DESTDIR: the directory tree and the headers    ~2 min
#	libs      libc and the run-time                          ~10 min
#	dirs      everything in port/build-list.txt              ~40 min
#	kernel    the kernel itself                              ~2 min
#	image     the ramdisk, kernel.bin and boot.mba           ~3 min
#	disk      a virtio-blk root image for QEMU               ~2 min
#
# Times are from a 24-core machine; the toolchain stages dominate and are
# each done once.  Every stage is idempotent: re-running it is cheap if
# nothing changed, and safe if it failed halfway.
#
# Why this is a script and not a single make target: the tree's own
# "distribution" target walks directories this port does not build yet, and
# stops at the first one.  The list of what does build is a fact about the
# state of the port, so it lives in a file -- port/build-list.txt.
set -uo pipefail

PORT=$(cd "$(dirname "$0")" && pwd)
SRCDIR=${SRCDIR:-$(cd "$PORT/.." && pwd)}

ARCH=evbarm64-el
JOBS=${JOBS:-$(nproc)}
OBJ=${OBJ:-$HOME/obj-evbarm64}
DEST=${DEST:-$HOME/dest-evbarm64}
TOOLS=${TOOLS:-$HOME/tools-evbarm64}
XTOOLS=${XTOOLS:-$HOME/xtools-aarch64}
MAKE=$TOOLS/bin/nbmake-$ARCH
LIST=${LIST:-$PORT/build-list.txt}

STAGES=("$@")
[ ${#STAGES[@]} -eq 0 ] && STAGES=(tools xtools includes libs dirs kernel image)

want() { for s in "${STAGES[@]}"; do [ "$s" = "$1" ] && return 0; done; return 1; }
say()  { printf '\n=== %s\n' "$*"; }
die()  { printf '\n!!! %s\n' "$*" >&2; exit 1; }

# ---------------------------------------------------------------------------
# The tree has to be on a real Unix file system.  Some names in the NetBSD
# part are illegal on NTFS, so a checkout under /mnt/c or /mnt/d silently
# loses files, and the failure surfaces much later as a missing header.
case "$SRCDIR" in
/mnt/[a-z]/*) die "the tree is on a Windows drive ($SRCDIR); clone it under \$HOME instead" ;;
esac

say "MEECHO build: ${STAGES[*]}"
echo "tree     $SRCDIR"
echo "objects  $OBJ"
echo "DESTDIR  $DEST"
echo "jobs     $JOBS"

# ---------------------------------------------------------------------------
if want tools; then
	say "tools: NetBSD host tools"
	JOBS=$JOBS bash "$PORT/build-minix.sh" $ARCH tools || die "tools failed (see ~/build-evbarm64.log)"
	[ -x "$MAKE" ] || die "no $MAKE after the tools stage"
fi

if want xtools; then
	say "xtools: cross toolchain for aarch64-elf64-minix"
	if [ -x "$XTOOLS/bin/aarch64-elf64-minix-gcc" ]; then
		echo "already built: $XTOOLS"
	else
		bash "$PORT/build-xtools.sh" all || die "toolchain failed"
	fi
fi

if want includes; then
	say "includes: DESTDIR tree and headers"
	bash "$PORT/tree-includes.sh" || die "includes failed (see ~/build-includes.log)"
fi

if want libs; then
	say "libs: the C run-time and libc"
	bash "$PORT/tree-libs.sh" lib/csu lib/libc || die "libc failed (see ~/build-libs.log)"
fi

# ---------------------------------------------------------------------------
if want dirs; then
	[ -f "$LIST" ] || die "no $LIST"
	mapfile -t dirs < <(grep -v '^#' "$LIST" | grep -v '^[[:space:]]*$')
	say "dirs: ${#dirs[@]} directories from $(basename "$LIST")"
	failed=()
	i=0
	for d in "${dirs[@]}"; do
		i=$((i + 1))
		[ -f "$SRCDIR/$d/Makefile" ] || continue
		log=/tmp/meecho-build-$(echo "$d" | tr / _).log
		if { "$MAKE" -C "$SRCDIR/$d" obj &&
		     "$MAKE" -C "$SRCDIR/$d" -j"$JOBS" dependall &&
		     "$MAKE" -C "$SRCDIR/$d" install; } > "$log" 2>&1
		then
			printf '\r  [%3d/%3d] ok   %-50s' "$i" "${#dirs[@]}" "$d"
		else
			printf '\r  [%3d/%3d] FAIL %-50s\n' "$i" "${#dirs[@]}" "$d"
			grep -m2 -E 'error:|Error code|cannot find|no rule to make' "$log" |
				sed "s|$SRCDIR/||" | cut -c1-140 | sed 's/^/           /'
			failed+=("$d")
		fi
	done
	printf '\r%-70s\n' " "
	if [ ${#failed[@]} -gt 0 ]; then
		echo "failed (${#failed[@]}): ${failed[*]}"
		echo "logs are in /tmp/meecho-build-*.log"
	else
		echo "all ${#dirs[@]} directories built"
	fi
fi

# ---------------------------------------------------------------------------
if want kernel; then
	say "kernel"
	"$MAKE" -C "$SRCDIR/minix/kernel" obj > /dev/null 2>&1
	"$MAKE" -C "$SRCDIR/minix/kernel" -k -j"$JOBS" dependall > /tmp/meecho-kernel.log 2>&1 ||
		{ grep -m5 -E 'error:|Error|undefined reference' /tmp/meecho-kernel.log; die "kernel failed"; }
	ls -l "$OBJ/minix/kernel/kernel"
fi

if want image; then
	say "image: ramdisk, kernel.bin, boot.mba"
	bash "$PORT/ramimage.sh" -b || die "image failed"
fi

if want disk; then
	say "disk: a root file system image for QEMU"
	DISK_FRESH=1 bash "$PORT/ramimage.sh" -d -n || die "disk failed"
fi

say "done"
echo "kernel.bin and boot.mba are in $OBJ/work"
echo "run it:  bash $PORT/ramimage.sh -r"
