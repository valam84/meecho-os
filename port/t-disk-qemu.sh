#!/bin/sh
PORT=$(cd "$(dirname "$0")" && pwd)
SRCDIR=${SRCDIR:-$(cd "$PORT/.." && pwd)}

{
	sleep 18; echo root
	sleep 3;  echo "mount; echo marker-\$\$ > /root/m; cat /root/m"
	sleep 4;  echo /sbin/poweroff
	sleep 6
} | QEMU_TIMEOUT=70 bash "$PORT/ramimage.sh" -d 2>&1
