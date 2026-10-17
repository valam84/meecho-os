#!/bin/bash
# A copy of the Mi Pad 4's vendor partition, read and only read, for the
# GPU's zap shader in it (hw-gpu.md, §14): the tablet up under MEECHO with
# gptro0=vendor on its command line, so that sdmmc serves that partition,
# read only, as /dev/c0d0p3 beside the root's window.  Nothing is written
# on the tablet.
#
#	vendor-pull.sh [-o FILE] [-d DEV]
#
#	-o FILE	where the copy goes (default D:/minix/clover-flash/gpu/
#		vendor.img - outside the tree: the files in it are Xiaomi's
#		and Qualcomm's)
#	-d DEV	the device (default /dev/c0d0p3)
#
# The copy is checked: its md5 against the one the tablet computes from the
# device, after the copy.  The kernel log's sdmmc lines come first, so that
# a missing window says why.  Where things are: port/board.conf, as for
# cycle.sh and gpu-probe.sh.
set -u
MSYS_NO_PATHCONV=1
export MSYS_NO_PATHCONV
HERE=$(cd "$(dirname "$0")" && pwd)
[ -f "$HERE/../board.conf" ] && . "$HERE/../board.conf"
HUB=${HUB:?port/board.conf: HUB=}
HUB_PORT=${HUB_PORT:-22}
KEY=${KEY:?port/board.conf: KEY=}
KEY=$(printf '%s\n' "$KEY" | tr '\134' '/')
CLOVER=${CLOVER:-root@10.42.0.2}
OUT=D:/minix/clover-flash/gpu/vendor.img
DEV=/dev/c0d0p3
while getopts o:d: o; do
	case $o in
	o) OUT=$OPTARG ;;
	d) DEV=$OPTARG ;;
	*) echo "usage: vendor-pull.sh [-o FILE] [-d DEV]" >&2; exit 2 ;;
	esac
done

HUB_O=(-o BatchMode=yes -o ConnectTimeout=15 -o IdentitiesOnly=yes -i "$KEY")
TAB_O=("${HUB_O[@]}" -o StrictHostKeyChecking=no
    -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR
    -o ServerAliveInterval=5 -o ServerAliveCountMax=6
    -o "ProxyCommand=ssh ${HUB_O[*]} -p $HUB_PORT -W %h:%p $HUB")
tab() { timeout "${T:-60}" ssh "${TAB_O[@]}" "$CLOVER" "$@"; }

v=$(tab 'uname -s; sysenv cycle' 2>&1 | tr -d '\r' | tr '\n' ' ')
case "$v" in
MEECHO*) echo "tablet: $v" ;;
*) echo "FAIL: the tablet does not answer as MEECHO ($v)"; exit 3 ;;
esac
tab "kmsg 2>/dev/null | grep -E 'sdmmc.*(gpt|window|read only|starts with)'; \
    ls -l $DEV" | tr -d '\r'
tab "test -b $DEV" || { echo "FAIL: no block device $DEV"; exit 3; }

echo "copying $DEV to $OUT"
start=$(date +%s)
T=3600 tab "dd if=$DEV bs=65536 2>/dev/null" > "$OUT" ||
    { echo "FAIL: the copy broke off ($(stat -c %s "$OUT") bytes)"; exit 4; }
end=$(date +%s)
size=$(stat -c %s "$OUT")
echo "$size bytes in $((end - start)) s"
here=$(md5sum < "$OUT" | cut -c1-32)
there=$(T=1200 tab "dd if=$DEV bs=65536 2>/dev/null | md5" | tr -d '\r ')
echo "md5 here $here, on the tablet $there"
[ "$here" = "$there" ] || { echo "FAIL: the copy differs"; exit 5; }
echo "ok: $OUT"
