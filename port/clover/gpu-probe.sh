#!/bin/bash
# The first round of the Adreno service on the Mi Pad 4, over ssh, without
# a person (port/clover/hw-gpu.md, §12): the service, its configuration and
# a minix-service that knows QCOMSCM go to /root/adreno on the tablet's
# root (no system file is replaced), then one stage per run, each answered
# before the next starts, the kernel log's new lines after each.
#
#	gpu-probe.sh [-d DIR] [-l LOG] [STAGE...]
#
#	-d DIR	where adreno, adreno.conf and minix-service are
#		(default D:/minix/clover-flash/gpu)
#	-l LOG	the log (default DIR/probe-<time>.txt)
#	STAGE	"probe+tz", "probe", "cx", "gx", "gpu", "smmu" (default all of
#		them, in that order)
#
# The tablet must be up under MEECHO with its root on eMMC and sshd -
# after port/clover/cycle.sh, whose image also carries the kernel with
# sys_qcomscm(2) when KERNEL_ELF= said so.  A stage after which the tablet
# no longer answers stops the round: the kernel's boot watchdog takes it to
# fastboot (nextboot=bootloader) and the hub's keeper parks it.
#
# Where things are comes from port/board.conf, as for cycle.sh.
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
DIR=D:/minix/clover-flash/gpu
log=
while getopts d:l: o; do
	case $o in
	d) DIR=$OPTARG ;;
	l) log=$OPTARG ;;
	*) echo "usage: gpu-probe.sh [-d DIR] [-l LOG] [STAGE...]" >&2; exit 2 ;;
	esac
done
shift $((OPTIND - 1))
[ $# -gt 0 ] || set -- probe+tz cx gx gpu smmu
log=${log:-$DIR/probe-$(date +%Y%m%d%H%M%S).txt}
: > "$log" || exit 2

HUB_O=(-o BatchMode=yes -o ConnectTimeout=15 -o IdentitiesOnly=yes -i "$KEY")
TAB_O=("${HUB_O[@]}" -o StrictHostKeyChecking=no
    -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR
    -o ServerAliveInterval=5 -o ServerAliveCountMax=3
    -o "ProxyCommand=ssh ${HUB_O[*]} -p $HUB_PORT -W %h:%p $HUB")
tab() { timeout "${T:-60}" ssh "${TAB_O[@]}" "$CLOVER" "$@"; }
say() { echo "$*" | tee -a "$log"; }

say "gpu-probe $(date '+%F %T')"
v=$(tab 'uname -s; sysenv cycle' 2>&1 | tr -d '\r' | tr '\n' ' ')
case "$v" in
MEECHO*) say "tablet: $v" ;;
*) say "FAIL: the tablet does not answer as MEECHO ($v)"; exit 3 ;;
esac

tab 'mkdir -p /root/adreno' || exit 3
scp -q "${TAB_O[@]}" "$DIR/adreno" "$DIR/adreno.conf" "$DIR/minix-service" \
    "$CLOVER:/root/adreno/" || { say "FAIL: upload"; exit 3; }
# scp from Windows brings no execute bit.
sums=$(tab 'cd /root/adreno && chmod 755 adreno minix-service &&
    md5 adreno adreno.conf minix-service' | tr -d '\r')
say "$sums"

for s in "$@"; do
	case $s in
	probe+tz) args="stage=probe tz=1" ;;
	probe|cx|gx|gpu|smmu) args="stage=$s" ;;
	*) say "FAIL: no stage $s"; exit 2 ;;
	esac
	say "--- $args"
	tab "kmsg > /tmp/adreno-k0 2>/dev/null; cd /root/adreno &&
	    ./minix-service up /root/adreno/adreno -config /root/adreno/adreno.conf \
	    -args '$args' 2>&1; sleep 1; kmsg > /tmp/adreno-k1 2>/dev/null;
	    diff /tmp/adreno-k0 /tmp/adreno-k1 | sed -n 's/^> //p'" 2>&1 |
	    tr -d '\r' | tee -a "$log"
	if ! T=20 tab true 2> /dev/null; then
		say "FAIL: the tablet stopped answering after \"$args\""
		exit 4
	fi
done
say "--- end; the tablet answers"
