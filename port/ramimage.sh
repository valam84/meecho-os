#!/bin/bash
# Wrapper around releasetools/evbarm64_ramimage.sh for this workstation:
# fills in the directories the tree build was given (-O/-D/-T) and the
# external toolchain, then passes every argument through.
#
#	bash ramimage.sh          assemble kernel.bin + boot.mba
#	bash ramimage.sh -b       build the ramdisk and memory driver first
#	bash ramimage.sh -r       assemble and run
#	bash ramimage.sh -2       assemble and run entering at EL2
set -u
PORT=$(cd "$(dirname "$0")" && pwd)
SRCDIR=${SRCDIR:-$(cd "$PORT/.." && pwd)}

export ARCH=evbarm64-el
export OBJ=${OBJ:-$HOME/obj-evbarm64}
export DESTDIR=${DESTDIR:-$HOME/dest-evbarm64}
export CROSS_TOOLS=${CROSS_TOOLS:-$HOME/tools-evbarm64/bin}
export EXTERNAL_TOOLCHAIN=${EXTERNAL_TOOLCHAIN:-$HOME/xtools-aarch64}
cd "$SRCDIR" || exit 1
exec bash releasetools/evbarm64_ramimage.sh "$@"
