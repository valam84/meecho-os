#!/bin/bash
# Step 1 of port/clover/hw-cpu.md on the Mi Pad 4, over ssh, without a
# person: what the boot chain leaves in the blocks that clock and power the
# CPU clusters - read, and only read.  The probe service (port/clover/
# cpuprobe) goes to /root/cpuprobe on the tablet's root (no system file is
# replaced), then one stage per run, each answered before the next starts;
# then the cores' speed, timed by a chain of dependent adds.
#
#	cpu-probe.sh -b			bring up the probe image (cycle.sh)
#	cpu-probe.sh [-d DIR] [-l LOG] [STAGE...]
#
#	-b	boot the image the stages need: the kit the tablet runs (the
#		kernel and boot archive below, copied to the WSL work
#		directory first), with sdm660-xiaomi-clover-cpu.dtsi in its
#		tree - through a DTS_TREE of its own, since cycle.sh passes no
#		other tree to mkboot.sh - and bootwd=1200, so that a reboot
#		that hangs costs twenty minutes, not an hour
#	-d DIR	where cpuprobe, cpuprobe.conf and cpuhz are
#		(default D:/minix/clover-flash/cpu)
#	-l LOG	the log (default DIR/probe-<time>.txt)
#	STAGE	fuse, pmic, rails, apcs, apm, osm, cprh (the service's stages, in that
#		order by default), hz (the cores' speed); "osm!" and "cprh!"
#		read those blocks even with their clock off (force=1) - only
#		when asked for by name.  Two more, never by default:
#		"cf:ARGS" runs the qcom_cpufreq service (hw-cpu.md, section
#		13; DIR/qcom_cpufreq and its .conf go to /root/qcom_cpufreq)
#		with ARGS, e.g. "cf:stage=set gold=1747" - stage=set WRITES
#		the clusters' PLL and mux, and only with the user's yes for
#		that step; "stress:SECONDS" keeps every core busy that long
#		with work whose result is known (gzip and gunzip of the same
#		random bytes, cpuhz's add count) and counts what came out
#		wrong
#
# BOOTWD= (default 1200) is the boot watchdog of the -b image, in seconds.
#
# A stage after which the tablet no longer answers stops the round: the
# kernel's boot watchdog takes it to fastboot (nextboot=bootloader) and the
# hub's keeper parks it.  The service runs off the boot processor
# (cpuprobe.conf, "cpu 1"), so a read that stalled the bus would stall that
# core and leave the watchdog's tick alone.
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
DIR=D:/minix/clover-flash/cpu
WSL_DISTRO=${WSL_DISTRO:-Ubuntu}
WSL_USER=${WSL_USER:-minix}
# The kit of the tablet's recent rounds (hw-gpu.md, section 20): this
# kernel objcopies to work-clover-kit2/kernel.bin.
KIT_ELF=${KIT_ELF:-/home/minix/obj-kvar/kit1017/minix/kernel/kernel}
KIT_MBA=${KIT_MBA:-/home/minix/work-clover-kit2/boot.mba}
W=/home/$WSL_USER/work-cpufreq
log=
boot=0
while getopts bd:l: o; do
	case $o in
	b) boot=1 ;;
	d) DIR=$OPTARG ;;
	l) log=$OPTARG ;;
	*) echo "usage: cpu-probe.sh -b | [-d DIR] [-l LOG] [STAGE...]" >&2
	   exit 2 ;;
	esac
done
shift $((OPTIND - 1))

if [ "$boot" = 1 ]; then
	# The tree: every file of the mainline one linked, the board's .dts
	# replaced by one that includes it and then the CPU blocks.
	cat > "//wsl.localhost/$WSL_DISTRO$W/tree.sh" <<EOF || exit 2
set -e
rm -rf $W/dts $W/kit
mkdir -p $W/dts/arch/arm64/boot/dts $W/kit
cp -as /home/$WSL_USER/sdm660-linux/arch/arm64/boot/dts/qcom $W/dts/arch/arm64/boot/dts/
ln -s /home/$WSL_USER/sdm660-linux/include $W/dts/include
Q=$W/dts/arch/arm64/boot/dts/qcom
rm \$Q/sdm660-xiaomi-clover.dts
printf '#include "%s"\n#include "%s"\n' \
    /home/$WSL_USER/sdm660-linux/arch/arm64/boot/dts/qcom/sdm660-xiaomi-clover.dts \
    /home/$WSL_USER/minix-src/port/clover/sdm660-xiaomi-clover-cpu.dtsi \
    > \$Q/sdm660-xiaomi-clover.dts
cp $KIT_ELF $W/kit/kernel
cp $KIT_MBA $W/kit/boot.mba
md5sum $W/kit/kernel $W/kit/boot.mba
EOF
	wsl -d "$WSL_DISTRO" -u "$WSL_USER" -- bash "$W/tree.sh" 2>&1 | tr -d '\0'
	[ "${PIPESTATUS[0]}" -eq 0 ] || { echo "FAIL: the tree"; exit 101; }
	KERNEL_ELF=$W/kit/kernel BOOT_MBA=$W/kit/boot.mba DTS_TREE=$W/dts \
	CMDLINE="rootdevname=c0d0 blkdrv=sdmmc gptpart0=userdata console=console osk=1 netdrv=dwc3_ecm netaddr=10.42.0.2/24 nextboot=bootloader bootwd=${BOOTWD:-1200}" \
	    bash "$HERE/cycle.sh" -c 'uname -a; sysenv cycle; uptime'
	exit $?
fi

[ $# -gt 0 ] || set -- fuse pmic rails apcs apm osm cprh hz
log=${log:-$DIR/probe-$(date +%Y%m%d%H%M%S).txt}
: > "$log" || exit 2

HUB_O=(-o BatchMode=yes -o ConnectTimeout=15 -o IdentitiesOnly=yes -i "$KEY")
TAB_O=("${HUB_O[@]}" -o StrictHostKeyChecking=no
    -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR
    -o ServerAliveInterval=5 -o ServerAliveCountMax=3
    -o "ProxyCommand=ssh ${HUB_O[*]} -p $HUB_PORT -W %h:%p $HUB")
tab() { timeout "${T:-60}" ssh "${TAB_O[@]}" "$CLOVER" "$@"; }
say() { echo "$*" | tee -a "$log"; }

say "cpu-probe $(date '+%F %T')"
v=$(tab 'uname -s; sysenv cycle' 2>&1 | tr -d '\r' | tr '\n' ' ')
case "$v" in
MEECHO*) say "tablet: $v" ;;
*) say "FAIL: the tablet does not answer as MEECHO ($v)"; exit 3 ;;
esac

tab 'mkdir -p /root/cpuprobe' || exit 3
scp -q "${TAB_O[@]}" "$DIR/cpuprobe" "$DIR/cpuprobe.conf" "$DIR/cpuhz" \
    "$CLOVER:/root/cpuprobe/" || { say "FAIL: upload"; exit 3; }
# scp from Windows brings no execute bit.
say "$(tab 'cd /root/cpuprobe && chmod 755 cpuprobe cpuhz &&
    md5 cpuprobe cpuprobe.conf cpuhz' | tr -d '\r')"

n=0
for s in "$@"; do
	n=$((n + 1))
	case $s in
	hz)
		# One copy, then one per core at once: with eight running, each
		# core has one, and ps says which.  The copies' results come in
		# two groups if the clusters run at different speeds.
		say "--- hz"
		T=300 tab 'cd /root/cpuprobe && cat /proc/cpuinfo; ./cpuhz;
		    for i in 1 2 3 4 5 6 7 8; do ./cpuhz > /tmp/hz.$i & done;
		    sleep 1; ps -ax -o pid,cpuid,command 2>&1 | grep -e cpuid -e cpuhz;
		    wait; for i in 1 2 3 4 5 6 7 8; do echo "copy $i:";
		    cat /tmp/hz.$i; done' 2>&1 | tr -d '\r' | tee -a "$log"
		;;
	fuse|pmic|rails|apcs|apm|osm|cprh|osm!|cprh!)
		args="stage=${s%!} tag=$n"
		case $s in *!) args="$args force=1" ;; esac
		say "--- $args"
		# kmsg -f from before the start: the ring holds 10 kB, and a
		# stage's lines must not scroll away before they are read.  Only
		# the lines from this run's own first line on.  /sbin by name: a
		# command over ssh gets no /sbin in its PATH.
		T=120 tab "cd /root/cpuprobe && { kmsg -f > /tmp/cpuprobe.k 2>&1 &
		    k=\$!; sleep 1; /sbin/minix-service up /root/cpuprobe/cpuprobe \
		    -config /root/cpuprobe/cpuprobe.conf -args '$args' 2>&1;
		    sleep 2; kill \$k; awk '/cpuprobe: stage ${s%!} \\(tag $n\\)/ { on = 1 }
		    on && /cpuprobe:/' /tmp/cpuprobe.k; }" 2>&1 |
		    tr -d '\r' | tee -a "$log"
		;;
	cf:*)
		# The service and its .conf go up once.  Its lines: everything
		# the kernel's ring gets after the start.  RS gives a service
		# 10 s (RS_INIT_T) to finish its init, and qcom_cpufreq does its
		# work in init: no hold= of 10 s or more.
		args=${s#cf:}
		if [ -z "${cf_up:-}" ]; then
			tab 'mkdir -p /root/qcom_cpufreq' || exit 3
			scp -q "${TAB_O[@]}" "$DIR/qcom_cpufreq" \
			    "$DIR/qcom_cpufreq.conf" "$CLOVER:/root/qcom_cpufreq/" ||
			    { say "FAIL: upload"; exit 3; }
			say "$(tab 'cd /root/qcom_cpufreq && chmod 755 qcom_cpufreq &&
			    md5 qcom_cpufreq qcom_cpufreq.conf' | tr -d '\r')"
			cf_up=1
		fi
		say "--- qcom_cpufreq $args"
		T=120 tab "cd /root/qcom_cpufreq && { kmsg -f > /tmp/cf.k 2>&1 &
		    k=\$!; sleep 1; n=\$(wc -l < /tmp/cf.k);
		    /sbin/minix-service up /root/qcom_cpufreq/qcom_cpufreq \
		    -config /root/qcom_cpufreq/qcom_cpufreq.conf -args '$args' 2>&1;
		    sleep 2; kill \$k; tail -n +\$((n + 1)) /tmp/cf.k; }" 2>&1 |
		    tr -d '\r' | tee -a "$log"
		;;
	stress:*)
		secs=${s#stress:}
		say "--- stress $secs s"
		cp "$HERE/cpu-stress.sh" "$DIR/cpu-stress.sh" &&
		    scp -q "${TAB_O[@]}" "$DIR/cpu-stress.sh" \
		    "$CLOVER:/root/cpuprobe/cpu-stress.sh" ||
		    { say "FAIL: upload"; exit 3; }
		T=$((secs + 120)) tab "sh /root/cpuprobe/cpu-stress.sh $secs 8;
		    echo \"stress: exit \$?\"" 2>&1 | tr -d '\r' | tee -a "$log"
		;;
	*) say "FAIL: no stage $s"; exit 2 ;;
	esac
	if ! T=20 tab true 2> /dev/null; then
		say "FAIL: the tablet stopped answering after \"$s\""
		exit 4
	fi
done
say "--- end; the tablet answers"
