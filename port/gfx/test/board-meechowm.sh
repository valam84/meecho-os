#!/bin/sh
#
# On the board, over ssh: the compositor on the monitor (stage 11.4).
#
# Starts meechowm on the frame buffer (FBIOC_MAP: the scanout buffers of
# the display driver) with foot in it, waits, and prints the screen's top
# left corner as text, read back through /dev/fb0 (fbart) - the same check
# as port/gfx/test/qemu-meechowm.py, on the real display controller this
# time.  Then it leaves the compositor running, for whoever is at the
# monitor to look and to use the mouse and the keyboard - Super+Return
# opens another foot as the next column of the strip, Super with the
# arrows goes along it, Super+d is the palette (the keys: meechowm.c), the
# panel is yambar, and "meechowm -m state" over ssh says where everything
# is; "board-meechowm.sh stop" ends it, and the console has the screen
# back.
#
#   sh board-meechowm.sh [stop]
#
# Expects the stack as the tree installs it (/usr/bin/meechowm, /usr/bin/foot,
# the keymaps, /etc/fonts), /usr/tests/gfx as qemu-meechowm.py stages it
# (fbart, fonts/, fonts.conf), the input server, xhci, usb_kbd and
# usb_mouse running (the eMMC root's rc starts them), and fb.
PATH=/bin:/sbin:/usr/bin:/usr/sbin
export PATH
G=/usr/tests/gfx

if [ "$1" = stop ]; then
	kill `cat /tmp/mwm.pid 2>/dev/null` 2>/dev/null
	sleep 3
	echo "stopped; the console should be back on the monitor"
	exit 0
fi

mkdir -p -m 700 /tmp/xdg
XDG_RUNTIME_DIR=/tmp/xdg
FONTCONFIG_FILE=$G/fonts.conf
MEECHOWM_TERM="foot -o main.workers=0 -f 'DejaVu Sans Mono:size=14'"
export XDG_RUNTIME_DIR FONTCONFIG_FILE MEECHOWM_TERM

# SIGHUP ignored: started from ssh, the compositor is in the session's
# process group, and the end of the ssh session hangs it up - on the first
# run it died the moment this script returned, and foot after it ("Broken
# pipe"), while the monitor went back to the console.
trap '' HUP
# The panel (meechowm-bar: yambar) along the top, and foot below it.
meechowm -s "meechowm-bar & foot -o main.workers=0 \
    -f 'DejaVu Sans Mono:size=14' \
    -e /bin/sh -c 'echo FOOT-OK on MEECHO; exec /bin/sh -i'" \
    < /dev/null > /tmp/mwm.log 2>&1 &
echo $! > /tmp/mwm.pid
sleep 15

echo "== meechowm"
grep -E "INFO|ERROR" /tmp/mwm.log | tail -12
echo "== the screen's top left"
$G/fbart -w 320 -h 64 -s 2
echo "left running (pid `cat /tmp/mwm.pid`): look at the monitor;"
echo "sh board-meechowm.sh stop ends it"
