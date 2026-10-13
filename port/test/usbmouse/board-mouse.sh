#!/bin/sh
#
# On the board, over ssh: what a real mouse sends, seen from the other
# side of the input server - the side the compositor will read.
#
# Somebody at the board moves the mouse, presses each of its three buttons
# and turns the wheel while this runs; mouseev (port/test/usbmouse,
# put on the board by port/board-put.sh or port/board-cc.sh) reads
# /dev/mousemux for SECS seconds and prints every event and a summary.
# Before and after, the state of the driver chain, so that a run with no
# events says why: no mouse announced, driver not running, no reports.
#
#   sh board-mouse.sh [SECS]       default 30
#
# Requires usb_mouse running (the eMMC root's rc starts it) and a mouse in
# the hub.
PATH=/bin:/sbin:/usr/bin:/usr/sbin
export PATH

SECS=${1:-30}
EV=${MOUSEEV:-/usr/bin/mouseev}

echo "== services"
minix-service status usb_mouse 2>&1 | head -3 ||
    echo "usb_mouse is not running"
ls -l /dev/mousemux /dev/mouse0

echo "== reading /dev/mousemux for $SECS s: move, click, scroll now"
"$EV" -t "$SECS" /dev/mousemux

echo "== the kernel's buffer, usb_mouse lines"
kmsg 2>/dev/null | grep -E "usb_mouse|xhci.*(announc|class 3)" | tail -20
