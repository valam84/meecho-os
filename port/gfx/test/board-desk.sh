#!/bin/sh
#
# On the board, over ssh: the MEECHO desktop as a user starts it -
# meecho-desktop (stage 11.7: the wallpaper, the panel and foot, in
# MEECHO's style) - waits for it, prints where everything is
# (meechowm -m state), and leaves it running for whoever is at the
# monitor.  "board-desk.sh stop" ends it, and the console has the screen
# back.  The screen as a PNG: board-shot.sh, from the host.
#
#   sh board-desk.sh [stop]
#
# Expects the desktop installed (meecho-desktop and what it starts, the
# Go fonts, /etc/xdg, /usr/share/meecho) and the input server, xhci,
# usb_kbd, usb_mouse and fb running - the eMMC root's rc starts them.
PATH=/bin:/sbin:/usr/bin:/usr/sbin
HOME=${HOME:-/root}
XDG_RUNTIME_DIR=/tmp/xdg-0
export PATH HOME XDG_RUNTIME_DIR

if [ "$1" = stop ]; then
	meechowm -m quit 2>/dev/null || kill `cat /tmp/desk.pid 2>/dev/null`
	sleep 3
	echo "stopped; the console should be back on the monitor"
	exit 0
fi

# SIGHUP ignored: started from ssh, the desktop is in the session's
# process group, and the end of the session would hang it up
# (board-meechowm.sh says how that looked).
trap '' HUP
meecho-desktop < /dev/null > /tmp/desk.log 2>&1 &
echo $! > /tmp/desk.pid

n=0
while [ $n -lt 60 ] && ! meechowm -m state 2>/dev/null |
    grep -q '^column 1\.1 .* title [^-]'; do
	sleep 2
	n=$((n + 1))
done
meechowm -m state
echo "left running (pid `cat /tmp/desk.pid`); sh board-desk.sh stop ends it"
