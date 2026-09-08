#!/bin/sh
PORT=$(cd "$(dirname "$0")" && pwd)
SRCDIR=${SRCDIR:-$(cd "$PORT/.." && pwd)}

{
	sleep 16; echo root
	sleep 3;  echo "/sbin/minix-service up /service/sdmmc -dev /dev/c1d0 -label sdmmc_0; echo rc=\$?"
	sleep 8;  echo "mount"
	sleep 3;  echo /sbin/poweroff
	sleep 6
} | QEMU_TIMEOUT=75 bash "$PORT/ramimage.sh" -r 2>&1
