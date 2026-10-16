#!/bin/bash
# On the hub: fastboot boot an image (default the nextboot one), then watch
# the bus until the tablet's boot loader is back in fastboot (or 300 s
# pass).  Step 1 of port/clover/hw-net.md; on the hub it lives in
# /root/meecho-clover/, next to the images.
set -u
cd /root/meecho-clover || exit 1
IMG=${1:-meecho-clover-nextboot.img}
t0=$(date +%s)
el() { echo $(( $(date +%s) - t0 )); }
dm0=$(dmesg | tail -1 | sed 's/^\[ *\([0-9]*\).*/\1/')
echo "t=0 fastboot boot $IMG"
timeout 60 fastboot boot $IMG 2>&1
present() { lsusb -d 18d1:d00d >/dev/null 2>&1; }
state=x
while [ $(el) -lt 300 ]; do
	if present; then s=fastboot; else s=gone; fi
	if [ "$s" != "$state" ]; then echo "t=$(el) $s"; state=$s; fi
	if [ "$s" = fastboot ] && [ $(el) -gt 20 ]; then
		timeout 10 fastboot getvar product 2>&1 | head -1
		break
	fi
	sleep 2
done
echo "t=$(el) end, state $state"
echo "--- dmesg since start"
dmesg | awk -v s="$dm0" '{ t=$0; sub(/^\[ */,"",t); sub(/\].*/,"",t); if (t+0 > s+0) print }' | tail -20
