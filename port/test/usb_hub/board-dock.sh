#!/bin/sh
#
# On the board, over ssh, in MEECHO: a USB-C dock in a port of the board's
# hub - a hub behind a hub, its Ethernet chip and card reader behind that.
# The USB chain is started again with what is in /service now (put there
# by board-put.sh: xhci, usb_hub, usb_storage, usb_kbd, usb_mouse,
# usb_ecm; and /etc/system.conf.d/usb_ecm), and the script waits for the
# second hub, then for cdce0.
#
#   sh board-dock.sh [ADDR [PEER]]
#
# Without ADDR the interface is only looked at: up, its address, and the
# frames it has received - the LAN's broadcasts are enough to show that
# frames come in through two hubs.  With ADDR it is given that address,
# and with PEER as well the far side is pinged (board-ecm.sh's frame
# sizes).  USBLOG= and ECMLOG= set the drivers' log levels (2: info).
#
# The network on the board's own Ethernet (dwmac, lwip, sshd) is not
# touched; the keyboard, the mouse and storage are started again behind
# the new xhci.
PATH=/bin:/sbin:/usr/bin:/usr/sbin
export PATH

ADDR=$1
PEER=$2
USBLOG=${USBLOG:-2}
ECMLOG=${ECMLOG:-2}

for s in usb_ecm usb_mouse usb_kbd usb_storage usb_hub usbd; do
	minix-service down $s 2>/dev/null
done
sleep 1

minix-service up /service/xhci -label usbd \
    -args "instance=0 log=$USBLOG" || { echo "xhci: did not start"; exit 1; }
sleep 2
minix-service up /service/usb_hub -label usb_hub ||
    { echo "usb_hub: did not start"; exit 1; }
minix-service up /service/usb_kbd -label usb_kbd -args log=2 ||
    echo "usb_kbd: did not start"
[ -x /service/usb_mouse ] && { minix-service up /service/usb_mouse \
    -label usb_mouse -args log=2 || echo "usb_mouse: did not start"; }
minix-service up /service/usb_storage -label usb_storage ||
    echo "usb_storage: did not start"
minix-service up /service/usb_ecm -label usb_ecm -args "log=$ECMLOG" ||
    { echo "usb_ecm: did not start"; exit 1; }

# The board's hub is found by xhci on the root port, the dock by the hub
# driver on one of the board's hub's ports, the Ethernet chip by the hub
# driver again on one of the dock's - two rounds of debounce, reset and
# enumeration.
n=0
while [ $n -lt 40 ]; do
	ifconfig cdce0 >/dev/null 2>&1 && break
	sleep 1
	n=$((n + 1))
done

echo "== what the drivers said"
kmsg 2>/dev/null | grep -E "usb_hub|xhci.*(hub|route|announc|withdraw|gone)|usb_ecm|usb_storage" |
    tail -60

if ! ifconfig cdce0 >/dev/null 2>&1; then
	echo "no cdce0 after 40 s"
	ifconfig -l
	exit 1
fi
echo "== cdce0 after $n s"

if [ -n "$ADDR" ]; then
	ifconfig cdce0 inet "$ADDR" up
else
	ifconfig cdce0 up
fi
sleep 10
ifconfig cdce0
netstat -i 2>/dev/null | grep -E "^Name|cdce0"

r=0
if [ -n "$PEER" ]; then
	ping -c 5 "$PEER" || r=1
	for s in 470 982 1472; do
		ping -c 3 -s $s "$PEER" || r=1
	done
	netstat -i 2>/dev/null | grep -E "^Name|cdce0"
fi
exit $r
