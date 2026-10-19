#!/bin/sh
# The governors measured on the battery alone (port/clover/hw-cpu.md, the
# governors' section): on the hub's cable the tablet charges, and below the
# charger's input the fuel gauge does not see what the system takes.  So a
# person takes the cable out, and the tablet runs every configuration by
# itself, in turn and in rounds - drift (the battery's voltage, the heat)
# falls on all of them alike - and writes the log to eMMC.  Start it with
# the cable in, from ssh:
#
#	( trap "" HUP; sh /root/gov/powerrun.sh ) > /dev/null 2>&1 &
#
# It waits up to WAIT_MIN minutes for VBUS to go (sysctl minix.pmic.vbus
# "none"), lets the unplugging settle, runs, and puts the policies back to
# what rc gives (ondemand, no GPU sleep).  The log is /root/powerrun.log,
# synced after every line; "powerrun: done" is its last.
# FORCE=1 does not wait for the cable: a dry run, with short IDLE_S=,
# LOAD_S=, ROUNDS=, SETTLE=.
#
# A configuration is the CPU's policy (qcom_cpufreq, restarted), the GPU's
# and its sleep (sysctl minix.gpu.gov, minix.gpu.sleep):
#   old    performance, performance, 0 - what every boot did before
#   new    ondemand, ondemand, 0 - what rc does now
#   sleep  ondemand, ondemand, 2000 ms
#   save   powersave, powersave, 0 - the floor of what clocks alone give
# A phase: the configuration set, its load started, SETTLE seconds, then a
# sample of sysctl minix.pmic.battery every two seconds for SECS; the line
# "phase NAME CONF: N samples, mean M mA, median D mA" (minus: out of the
# battery), and the samples.
PATH=/bin:/sbin:/usr/bin:/usr/sbin
L=/root/powerrun.log
WAIT_MIN=${WAIT_MIN:-20}
SETTLE=${SETTLE:-10}
IDLE_S=${IDLE_S:-100}
LOAD_S=${LOAD_S:-60}
ROUNDS=${ROUNDS:-3}

up() { cut -d' ' -f1 /proc/uptime; }
say() { echo "`up` $*" >> $L; sync; }

# (sh has no local variables: these names are conf's alone.)
conf() {
	case $1 in
	old)	cf_gov=performance cf_ggov=performance cf_sleep=0 ;;
	new)	cf_gov=ondemand cf_ggov=ondemand cf_sleep=0 ;;
	sleep)	cf_gov=ondemand cf_ggov=ondemand cf_sleep=2000 ;;
	save)	cf_gov=powersave cf_ggov=powersave cf_sleep=0 ;;
	esac
	# down is not over when it returns: up again only once it is gone.
	minix-service down qcom_cpufreq 2> /dev/null
	cf_i=0
	while ps ax | grep -q "[/]service/qcom_cpufreq" && [ $cf_i -lt 20 ]
	do
		sleep 0.5
		cf_i=$((cf_i + 1))
	done
	minix-service up /service/qcom_cpufreq \
	    -args "stage=gov gov=$cf_gov gold=1747 silver=1536" 2> /dev/null ||
	    say "  qcom_cpufreq did not come up for $cf_gov"
	sysctl -w minix.gpu.gov=$cf_ggov > /dev/null
	sysctl -w minix.gpu.sleep=$cf_sleep > /dev/null
}

phase() {	# NAME CONF LOAD SECS
	conf $2
	pids=
	case $3 in
	cube)	/usr/tests/gles/glcube -o -q -n 100000000 > /dev/null 2>&1 &
		pids=$! ;;
	cpu)	awk 'BEGIN { for (;;) x++ }' & pids=$! ;;
	esac
	sleep $SETTLE
	: > /tmp/pr.samples
	n=0
	while [ $n -lt $(($4 / 2)) ]; do
		sysctl -n minix.pmic.battery >> /tmp/pr.samples
		sleep 2
		n=$((n + 1))
	done
	for p in $pids; do
		kill $p 2> /dev/null
	done
	res=`sort -n -k3 /tmp/pr.samples | awk '{ v[NR] = $3; s += $3; st = $4
	    mv = $2; soc = $1 } END { if (NR == 0) { print "no samples";
	    exit } printf "%d samples, mean %.1f mA, median %d mA; %d%%, %d mV, %s",
	    NR, s / NR, v[int((NR + 1) / 2)], soc, mv, st }'`
	say "phase $1 $2 $3: $res"
	say "  samples: `awk '{ printf "%s ", $3 }' /tmp/pr.samples`"
	say "  gpu: `sysctl -n minix.gpu.status`"
}

: > $L
say "powerrun: waiting for the cable to come out (up to $WAIT_MIN min)"
t=0
while [ -z "${FORCE:-}" ] && [ "`sysctl -n minix.pmic.vbus`" != none ]; do
	sleep 5
	t=$((t + 5))
	if [ $t -ge $((WAIT_MIN * 60)) ]; then
		say "powerrun: the cable stayed in; nothing measured"
		exit 1
	fi
done
say "powerrun: the cable is out; `sysctl -n minix.pmic.battery`"
sleep 30

round=1
while [ $round -le $ROUNDS ]; do
	for c in old new sleep save; do
		phase idle-$round $c idle $IDLE_S
	done
	if [ $round -le 2 ]; then
		for c in old new; do
			phase cube-$round $c cube $LOAD_S
			phase cpu-$round $c cpu $LOAD_S
		done
	fi
	round=$((round + 1))
done

conf new
say "powerrun: done; `sysctl -n minix.pmic.battery`"
