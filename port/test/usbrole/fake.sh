#!/bin/sh
# The usbrole stand's stand-ins for MINIX's commands, one script under the
# names it is linked as (run.sh makes the links).  Everything they know and
# do is in $STATE:
#
#   env/NAME	a boot parameter (sysenv)
#   typec	what qcom_pmic would say is at the other end; no file: no PMIC
#   otg		minix.pmic.otg
#   running	the labels minix-service has up, one a line
#   log		every action, in order
#   violation	written when both roles' drivers are up at once
#   ticks	how many sleeps may finish; slept: how many began
me=$(basename "$0")
S=$STATE

log() {
	echo "$*" >> "$S/log"
}

case $me in
sysenv)
	[ -f "$S/env/$1" ] || exit 1
	cat "$S/env/$1"
	;;
sysctl)
	if [ "$1" = -w ]
	then
		case $2 in
		minix.pmic.otg=*)
			[ -f "$S/typec" ] || exit 1
			echo "${2#*=}" > "$S/otg"
			log "sysctl otg=${2#*=}"
			exit 0 ;;
		esac
		exit 1
	fi
	[ "$1" = -n ] && shift
	[ -f "$S/typec" ] || exit 1	# no PMIC driver
	case $1 in
	minix.pmic.typec)	cat "$S/typec" ;;
	minix.pmic.vbus)	echo external ;;
	minix.pmic.otg)		cat "$S/otg" 2>/dev/null || echo off ;;
	*)			exit 1 ;;
	esac
	;;
minix-service)
	cmd=$1
	shift
	if [ "$cmd" = up ]
	then
		label=$(basename "$1")
		while [ $# -gt 0 ]
		do
			[ "$1" = -label ] && label=$2
			shift
		done
		if grep -qx "$label" "$S/running" 2>/dev/null
		then
			log "up $label: already running"
			exit 1
		fi
		echo "$label" >> "$S/running"
		log "up $label"
	else
		label=$1
		grep -qx "$label" "$S/running" 2>/dev/null || exit 1
		grep -vx "$label" "$S/running" > "$S/running.new"
		mv "$S/running.new" "$S/running"
		log "down $label"
	fi
	if grep -qx usbd "$S/running" && grep -qx dwc3_ecm_0 "$S/running"
	then
		echo "both up after: $cmd $label" >> "$S/violation"
	fi
	;;
ifconfig)
	if [ "$1" = -l ]
	then
		if grep -qx dwc3_ecm_0 "$S/running" 2>/dev/null
		then	echo "lo0 ecm0"
		else	echo "lo0"
		fi
		exit 0
	fi
	log "addr $1 $3"
	;;
sleep)
	n=$(( $(cat "$S/slept" 2>/dev/null || echo 0) + 1 ))
	echo $n > "$S/slept"
	while [ "$(cat "$S/ticks" 2>/dev/null || echo 0)" -lt $n ]
	do
		/bin/sleep 0.01
	done
	;;
esac
exit 0
