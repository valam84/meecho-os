#!/bin/sh
#
# On the board, over ssh, in MEECHO: the USB chain started again with what
# is in /service now (xhci and usb_ecm put there by board-put.sh), and a
# CDC Ethernet device on one of the hub's ports - the Mi Pad 4 on a cable
# with its own dwc3_ecm at 10.42.0.2, or an adapter.  The interface cdce0
# gets an address and the far side is pinged.
#
#   sh board-ecm.sh [ADDR [PEER]]        10.42.0.1/24 and 10.42.0.2
#
# ECMLOG= and USBLOG= set the drivers' log levels (2: info).  The network
# on the board's own Ethernet (dwmac, lwip, sshd) is not touched; the
# keyboard and the mouse are started again behind the new xhci.
PATH=/bin:/sbin:/usr/bin:/usr/sbin
export PATH

ADDR=${1:-10.42.0.1/24}
PEER=${2:-10.42.0.2}
ECMLOG=${ECMLOG:-2}
USBLOG=${USBLOG:-2}

for s in usb_ecm usb_mouse usb_kbd usb_storage usb_hub usbd; do
	minix-service down $s 2>/dev/null
done
sleep 1

minix-service up /service/xhci -label usbd \
    -args "instance=0 log=$USBLOG" || { echo "xhci: did not start"; exit 1; }
sleep 2
minix-service up /service/usb_hub -label usb_hub || echo "usb_hub: did not start"
minix-service up /service/usb_kbd -label usb_kbd -args log=2 ||
    echo "usb_kbd: did not start"
[ -x /service/usb_mouse ] && { minix-service up /service/usb_mouse \
    -label usb_mouse -args log=2 || echo "usb_mouse: did not start"; }
minix-service up /service/usb_ecm -label usb_ecm -args "log=$ECMLOG" ||
    { echo "usb_ecm: did not start"; exit 1; }

# The device is behind the board's hub: the hub driver has to find it,
# xhci enumerate it, usb_ecm ask its six questions.
n=0
while [ $n -lt 30 ]; do
	ifconfig cdce0 >/dev/null 2>&1 && break
	sleep 1
	n=$((n + 1))
done
if ! ifconfig cdce0 >/dev/null 2>&1; then
	echo "no cdce0 after 30 s"
	ifconfig -l
	exit 1
fi
echo "cdce0 after $n s"

ifconfig cdce0 inet "$ADDR" up
ifconfig cdce0
ping -c 5 "$PEER"
r=$?
# Frames of 512 and 1024 bytes - a whole number of packets, which goes as
# one byte more - and a full one of 1514.
for s in 470 982 1472; do
	ping -c 3 -s $s "$PEER" || r=1
done
netstat -i 2>/dev/null | grep -E "^Name|cdce0"
exit $r
