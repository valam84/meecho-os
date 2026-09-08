#!/bin/sh
PORT=$(cd "$(dirname "$0")" && pwd)
SRCDIR=${SRCDIR:-$(cd "$PORT/.." && pwd)}

{
	sleep 18; echo root
	sleep 3;  echo "/sbin/mkfs.mfs"
	sleep 4;  echo "/sbin/mkfs.mfs -4 /tmp/fsimg 200"
	sleep 5;  echo "ls -l /tmp/fsimg; /bin/fsck_mfs -s /tmp/fsimg"
	sleep 5;  echo /sbin/poweroff
	sleep 6
} | QEMU_TIMEOUT=80 bash "$PORT/ramimage.sh" -d 2>&1
