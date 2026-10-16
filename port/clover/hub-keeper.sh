#!/bin/bash
# On the hub: keep the Mi Pad 4 out of fastboot's idle power-off.
#
# The boot loader switches the tablet off after a while in fastboot, and
# then only a person at its buttons brings it back.  So the tablet is kept
# in MEECHO instead: whenever its boot loader shows up on the bus, this
# boots the oldest image in queue/ (moved to done/ once fastboot took it)
# or, with nothing queued, the parking image - one that sets
# nextboot=bootloader and resets through bootwd, so the tablet is back in
# fastboot a couple of minutes later and the next queued image waits no
# longer than that.  port/clover/hw-net.md, step 1.
#
#	nohup setsid bash hub-keeper.sh &	(in /root/meecho-clover)
#	cp test.img queue/			to try an image
#	touch stop				to stop it; it also stops by
#						itself after HOURS hours
#
# While it runs, putting the tablet into fastboot by hand gets an image
# booted on it within seconds: touch stop first.
set -u
cd /root/meecho-clover || exit 1
PARK=${PARK:-meecho-clover-park.img}	# nextboot=bootloader bootwd=600
END=$(( $(date +%s) + ${HOURS:-6} * 3600 ))
LOG=keeper.log
mkdir -p queue done
rm -f stop
echo "$(date '+%F %T') keeper starts (park $PARK, until $(date -d @$END +%T))" >> $LOG
while [ "$(date +%s)" -lt "$END" ] && [ ! -e stop ]; do
	if lsusb -d 18d1:d00d > /dev/null 2>&1; then
		img=$(ls -1tr queue/*.img 2> /dev/null | head -1)
		target=${img:-$PARK}
		echo "$(date '+%F %T') fastboot -> $target" >> $LOG
		if timeout 60 fastboot boot "$target" >> $LOG 2>&1; then
			[ -n "$img" ] && mv "$img" done/
		fi
		sleep 10		# let it leave the bus
	fi
	sleep 2
done
echo "$(date '+%F %T') keeper stops" >> $LOG
