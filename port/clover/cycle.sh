#!/bin/bash
# One round on the Mi Pad 4 (clover) under MEECHO, without a person: build
# a boot.img, put it in the hub keeper's queue, send the tablet to fastboot
# over ssh, wait until that image answers on ssh, run something there and
# keep the log.  Step 5 of port/clover/hw-net.md.
#
#	cycle.sh [-i boot.img] [-c command | -s script] [-l log] [-t seconds]
#
#	-i IMG	boot this image instead of building one; it must bring up
#		ssh, and say cycle= (below) - otherwise the round cannot tell
#		it from the image before
#	-c CMD	run CMD on the tablet
#	-s FILE	run FILE there with sh
#		(default: uname, uptime, mount and the kernel's log)
#	-l LOG	the log (default: cycle-<id>.log in the build directory)
#	-t SEC	how long the keeper may take to boot the image (default 900:
#		the parking image comes back to fastboot within ten minutes)
#
# The tablet's way there and back (hw-net.md, steps 1, 4c and 5):
#   - it lives in MEECHO, never idle in fastboot: the boot loader switches
#     off there after a while, and only its buttons bring it back.  On the
#     hub hub-keeper.sh boots the oldest image of queue/ whenever fastboot
#     shows up, or the parking image (nextboot=bootloader bootwd=600);
#   - an image with sshd up is reached as root@10.42.0.2 through the hub
#     (the tablet's USB-C port is a CDC Ethernet device, the hub keeps
#     10.42.0.1 for it); /sbin/reboot there goes to fastboot, because
#     qcom_pmic sets the PMIC's next-boot register from nextboot=;
#   - an image that does not come up resets through its bootwd, back to
#     fastboot, and the keeper parks the tablet.  Only a hang before the
#     kernel's watchdog runs needs the power button.
#
# The build is port/clover/mkboot.sh with its variables: KERNEL_ELF,
# BOOT_MBA (both as the build sees them), DTS_TREE, CMDLINE.  CMDLINE
# defaults to the root on eMMC with the network (hw-net.md, step 4c), and
# always gets " cycle=<id>" appended: the tablet answers "sysenv cycle"
# with it, and only that answer counts as "the image is up" - an ssh that
# answers may still be the image before.
#
# Where things are comes from port/board.conf, as for board-cycle.sh: HUB,
# HUB_PORT, KEY; WSL_DISTRO and WSL_USER when the tree builds in WSL and
# this runs from Git Bash.  Here also CLOVER (root@10.42.0.2), HUB_DIR
# (/root/meecho-clover) and KEEP_HOURS (4, when the keeper has to be
# started).
#
# Exit status: the command's own, once the image answered; 101 the build
# failed, 102 the hub refused or the upload did not arrive whole, 103 the
# keeper did not boot the image in time, 104 the image booted but never
# answered on ssh.
set -u

# Git Bash rewrites arguments that look like Unix paths when it calls a
# non-MSYS program (ssh, scp, wsl): "/sbin/reboot" would reach the tablet
# as "C:/Program Files/Git/sbin/reboot".
MSYS_NO_PATHCONV=1
export MSYS_NO_PATHCONV

HERE=$(cd "$(dirname "$0")" && pwd)
[ -f "$HERE/../board.conf" ] && . "$HERE/../board.conf"
HUB=${HUB:?port/board.conf: HUB=root@the-hub (see board.conf.example)}
HUB_PORT=${HUB_PORT:-22}
KEY=${KEY:?port/board.conf: KEY=the ssh key (see board.conf.example)}
# board.conf names the key through $USERPROFILE, with backslashes, and
# ProxyCommand runs through a shell that eats them.
KEY=$(printf '%s\n' "$KEY" | tr '\134' '/')
CLOVER=${CLOVER:-root@10.42.0.2}
HUB_DIR=${HUB_DIR:-/root/meecho-clover}
KEEP_HOURS=${KEEP_HOURS:-4}
WSL_DISTRO=${WSL_DISTRO:-}
WSL_USER=${WSL_USER:-}

img= cmd= script= log= take_max=900
BOOT_MAX=${BOOT_MAX:-300}	# fastboot boot -> ssh answers
usage() {
	echo "usage: cycle.sh [-i boot.img] [-c command | -s script] [-l log] [-t seconds]" >&2
	exit 2
}
while [ $# -gt 0 ]; do
	case "$1" in
	-i) img=${2:?}; shift 2 ;;
	-c) cmd=${2:?}; shift 2 ;;
	-s) script=${2:?}; shift 2 ;;
	-l) log=${2:?}; shift 2 ;;
	-t) take_max=${2:?}; shift 2 ;;
	*) usage ;;
	esac
done
[ -n "$cmd" ] && [ -n "$script" ] && usage
[ -z "$script" ] || [ -f "$script" ] || { echo "no $script" >&2; exit 2; }

ID=$(date +%Y%m%d%H%M%S)
t0=$(date +%s)
el() { echo $(( $(date +%s) - t0 )); }

# The hub: its host key is checked as for any machine.  The tablet: not -
# its host key is made on the first boot of every root written to it, so
# known_hosts would only say which root was there last.
HUB_O=(-o BatchMode=yes -o ConnectTimeout=15 -o IdentitiesOnly=yes -i "$KEY")
TAB_O=("${HUB_O[@]}" -o StrictHostKeyChecking=no
    -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR
    -o ServerAliveInterval=5 -o ServerAliveCountMax=3
    -o "ProxyCommand=ssh ${HUB_O[*]} -p $HUB_PORT -W %h:%p $HUB")
hub_ssh() { ssh "${HUB_O[@]}" -p "$HUB_PORT" "$HUB" "$@"; }
tab_ssh() { ssh "${TAB_O[@]}" "$CLOVER" "$@"; }

# The build directory as the build sees it, and as this script does.
if [ -n "$WSL_DISTRO" ]; then
	BHOME=/home/${WSL_USER:-$USER}
	WORK=${CYCLE_WORK:-$BHOME/work-clover-cycle}
	LWORK=${UNC:-//wsl.localhost/$WSL_DISTRO}$WORK
	SRC=${SRCDIR:-$BHOME/minix-src}
else
	WORK=${CYCLE_WORK:-$HOME/work-clover-cycle}
	LWORK=$WORK
	SRC=$(cd "$HERE/../.." && pwd)
fi
# MSYS's mkdir -p cannot make a directory under //wsl.localhost: it tries
# to make the server first.
if [ -n "$WSL_DISTRO" ]; then
	wsl -d "$WSL_DISTRO" ${WSL_USER:+-u "$WSL_USER"} -- mkdir -p "$WORK"
else
	mkdir -p "$WORK"
fi
[ -d "$LWORK" ] || { echo "FAIL: no $LWORK" >&2; exit 101; }
log=${log:-$LWORK/cycle-$ID.log}
: > "$log" || exit 2
say() { echo "t=$(el) $*" | tee -a "$log"; }

say "cycle $ID"

# 1. The boot image.
if [ -z "$img" ]; then
	: ${CMDLINE:=rootdevname=c0d0 blkdrv=sdmmc gptpart0=userdata console=console osk=1 netdrv=dwc3_ecm netaddr=10.42.0.2/24 nextboot=bootloader bootwd=1800}
	case " $CMDLINE " in
	*" nextboot=bootloader "*) ;;
	*) say "WARNING: CMDLINE has no nextboot=bootloader: if this image" \
	       "hangs, its bootwd reboots into the boot partition, not fastboot" ;;
	esac
	B=$WORK/build
	OUTI=$WORK/boot-$ID.img
	{
		echo "set -e"
		echo "cd '$SRC'"
		echo "export WORK_DIR='$B'"
		[ -n "${KERNEL_ELF:-}" ] && echo "export KERNEL_ELF='$KERNEL_ELF'"
		[ -n "${BOOT_MBA:-}" ] && echo "export BOOT_MBA='$BOOT_MBA'"
		[ -n "${DTS_TREE:-}" ] && echo "export DTS_TREE='$DTS_TREE'"
		echo "export CMDLINE='$CMDLINE cycle=$ID'"
		echo "bash port/clover/mkboot.sh '$OUTI'"
		echo "python3 port/clover/checkboot.py '$OUTI' --kernel \"\$WORK_DIR/kernel.bin\" --ramdisk \"\$WORK_DIR/boot.mba\" --dtb \"\$WORK_DIR/\${DTS:-sdm660-xiaomi-clover}.dtb\""
		# 17 MB a round: the last five images stay, with every log.
		echo "cd '$WORK' && ls -1t boot-*.img | tail -n +6 | xargs -r rm -f"
	} > "$LWORK/build-$ID.sh"
	if [ -n "$WSL_DISTRO" ]; then
		# A file, not a command line: quoting through wsl.exe is lost
		# silently, and so is $? in such a line.
		wsl -d "$WSL_DISTRO" ${WSL_USER:+-u "$WSL_USER"} -- \
		    bash "$WORK/build-$ID.sh" 2>&1 | tr -d '\0' >> "$log"
		st=${PIPESTATUS[0]}
	else
		bash "$LWORK/build-$ID.sh" >> "$log" 2>&1
		st=$?
	fi
	tail -3 "$log"
	[ "$st" -eq 0 ] || { say "FAIL: build ($log)"; exit 101; }
	img=$LWORK/boot-$ID.img
	say "built $(basename "$img")"
else
	say "image $img (not built here; it must say cycle=$ID to be told apart)"
fi
[ -s "$img" ] || { say "FAIL: no $img"; exit 101; }
sum=$(sha256sum < "$img" | cut -c1-64)

# 2. The hub: the keeper must be running, or nothing boots the image.
hub_ssh "sh -s" > "$LWORK/hub-$ID.txt" 2>&1 <<EOF
cd $HUB_DIR || exit 1
mkdir -p queue done
if pgrep -fx 'bash hub-keeper.sh' > /dev/null; then
	echo "keeper running"
else
	rm -f stop
	HOURS=$KEEP_HOURS nohup setsid bash hub-keeper.sh > /dev/null 2>&1 < /dev/null &
	sleep 1
	echo "keeper started for ${KEEP_HOURS}h"
fi
n=\$(ls queue/*.img 2> /dev/null | wc -l)
[ "\$n" -gt 0 ] && echo "queue already holds \$n image(s); this one waits behind"
true
EOF
st=$?
sed "s/^/t=$(el) hub: /" "$LWORK/hub-$ID.txt" | tee -a "$log"
[ "$st" -eq 0 ] || { say "FAIL: the hub ($st)"; exit 102; }

# 3. Into the queue: under a name the keeper does not take, checked, then
# renamed - a half-sent image must never be booted.
q=queue/cycle-$ID.img
scp -q "${HUB_O[@]}" -P "$HUB_PORT" "$img" "$HUB:$HUB_DIR/queue/.cycle-$ID.part" ||
	{ say "FAIL: upload"; exit 102; }
r=$(hub_ssh "cd $HUB_DIR && sha256sum < queue/.cycle-$ID.part | cut -c1-64")
[ "$r" = "$sum" ] || { say "FAIL: the hub has $r, not $sum"; exit 102; }
hub_ssh "cd $HUB_DIR && mv queue/.cycle-$ID.part $q" || exit 102
say "queued $q (sha256 ${sum:0:16})"

# 4. To fastboot: from MEECHO over ssh, if it answers; otherwise the
# keeper takes the image the next time the tablet shows up in fastboot.
was=$(tab_ssh 'uname -s; sysenv cycle' 2> /dev/null | tr -d '\r' | tr '\n' ' ')
case "$was" in
MEECHO*)
	say "tablet on ssh (cycle: ${was#MEECHO }); sending it to fastboot"
	# The register first, whatever the running image was booted with;
	# the reboot in the background, immune to the hangup the session's
	# end sends.  The session itself does not end before the reboot
	# takes the network (25 s, measured), so it gets 12 s: the register's
	# answer is all that is wanted from it.
	timeout 12 ssh "${TAB_O[@]}" "$CLOVER" '/sbin/sysctl -w minix.pmic.nextboot=bootloader &&
	    sync && { (trap "" HUP; sleep 2; /sbin/reboot) < /dev/null > /dev/null 2>&1 & }' 2>&1 |
	    tr -d '\r' | sed "s/^/t=$(el) tablet: /" | tee -a "$log"
	;;
*)
	say "tablet not on ssh: the keeper boots the image when fastboot shows up"
	;;
esac

# 5. The keeper takes it: gone from the queue means "fastboot boot" said
# OKAY (a failed boot stays queued and is tried again).  Asked for an
# answer, not by status: an ssh that failed must not read as "gone".
while :; do
	r=$(hub_ssh "[ -e $HUB_DIR/$q ] && echo queued || echo gone" 2> /dev/null)
	[ "$r" = gone ] && break
	if [ "$(el)" -ge "$take_max" ]; then
		# Taken out again: an image left queued would boot later, when
		# nobody waits for it.
		hub_ssh "rm -f $HUB_DIR/$q"
		say "FAIL: not booted in ${take_max} s (${r:-no answer from the hub}); taken off the queue"
		exit 103
	fi
	sleep 5
done
line=$(hub_ssh "grep -F 'cycle-$ID' $HUB_DIR/keeper.log | tail -1")
say "booted by the keeper: $line"
# The time the keeper wrote, not the time this loop noticed.
tb=$(date -d "$(echo "$line" | cut -c1-19)" +%s 2> /dev/null) || tb=$(date +%s)
hub_ssh "rm -f $HUB_DIR/done/cycle-$ID.img"

# 6. Up when it says its own cycle=.
while :; do
	now=$(tab_ssh 'sysenv cycle' 2> /dev/null | tr -d '\r')
	[ "$now" = "$ID" ] && break
	if [ $(( $(date +%s) - tb )) -ge "$BOOT_MAX" ]; then
		say "FAIL: booted $(( $(date +%s) - tb )) s ago, no answer on ssh"
		say "the hub's bus, last lines:"
		hub_ssh "dmesg | tail -8; ip -br link show usb0 2>&1" 2>&1 |
		    sed 's/^/    /' | tee -a "$log"
		exit 104
	fi
	sleep 5
done
say "on ssh $(( $(date +%s) - tb )) s after fastboot boot"

# 7. The run, and what the kernel's log says after it.
say "--- run"
if [ -n "$script" ]; then
	tab_ssh 'sh -s' < "$script" 2>&1 | tr -d '\r' | tee -a "$log"
	st=${PIPESTATUS[0]}
elif [ -n "$cmd" ]; then
	tab_ssh "$cmd" 2>&1 | tr -d '\r' | tee -a "$log"
	st=${PIPESTATUS[0]}
else
	tab_ssh 'uname -a; uptime; mount' 2>&1 | tr -d '\r' | tee -a "$log"
	st=${PIPESTATUS[0]}
fi
say "--- status $st"
echo "--- kmsg" >> "$log"
tab_ssh 'if [ -x /usr/bin/kmsg ]; then kmsg; else echo "(no /usr/bin/kmsg)"; fi' 2>&1 |
	tr -d '\r' >> "$log"
[ -n "$cmd$script" ] || sed -n '/^--- kmsg$/,$p' "$log" | tail -25
say "done: $log"
exit "$st"
