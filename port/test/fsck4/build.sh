#!/bin/sh
# Build the MFS V4 checker for the build host: fsck4.c from the tree plus
# the tiny main() next to this script.  The checker uses only pread, pwrite
# and stdio, so this is the same code the system runs as fsck_mfs on a V4
# volume - which makes it the tool for an image taken off a real disk.
#
#   sh port/test/fsck4/build.sh [output]      (default ~/fsck4-host)
#   ~/fsck4-host -n image                    read-only: report, change nothing
#   ~/fsck4-host -a image                    repair
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$(cd "$HERE/../../.." && pwd)
OUT=${1:-$HOME/fsck4-host}
gcc -O -Wall -o "$OUT" "$HERE/fsck4main.c" "$SRC/minix/commands/fsck.mfs/fsck4.c" \
	-I"$SRC/minix/commands/fsck.mfs" -I"$SRC/minix/fs" -I"$SRC/sbin/fsck" \
	-I"$SRC/minix/include"
echo "$OUT"
