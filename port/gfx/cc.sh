#!/bin/bash
#
# The cross compiler for the Wayland stack: aarch64-elf64-minix-gcc, with
# one thing changed.  The port's GCC refuses -pthread on purpose ("MINIX
# has no libpthread": a configure script that saw it accepted would build
# thread-using code for a system without kernel threads).  The stack asks
# for it anyway - meson's threads dependency, wayland-client.pc - and in the
# gfx sysroot there is a libpthread: libmthread under the POSIX names
# (build-stack.sh, "sysroot").  So here, and only here, -pthread links
# that and compiles as nothing.
#
# CC=port/gfx/cc.sh ... ; the cross file names it.
TOOLS=${GFX_TOOLS:-$HOME/xtools-aarch64/bin/aarch64-elf64-minix-}

link=1
for a in "$@"; do
	case "$a" in
	-c|-S|-E|-M|-MM) link=0 ;;
	esac
done

args=()
for a in "$@"; do
	if [ "$a" = -pthread ]; then
		[ $link -eq 1 ] && args+=(-lpthread)
		continue
	fi
	args+=("$a")
done
exec "${TOOLS}gcc" "${args[@]}"
