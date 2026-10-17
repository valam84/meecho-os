#!/bin/sh
# The Mi Pad 4's USB-C port as a host for a while, and a device again
# (port/clover/hw-usbhost.md, step 3) - run on the tablet, detached,
# because the network to the hub is this very port:
#
#	ssh clover-usb '( trap "" HUP; sh /root/usbhost-probe.sh 20 ) \
#	    </dev/null >/dev/null 2>&1 &'
#
# and read afterwards, over the network that came back:
#
#	ssh clover-usb 'cat /root/usbhost-probe.log; kmsg | grep -a xhci'
#
# dwc3_ecm goes down (lwip drops its interface), xhci comes up on the
# same core with the Qualcomm glue for N seconds (20) and goes down again
# - leaving the core in device mode - and dwc3_ecm comes back with the
# address rc.clover gives it.  On the hub's cable the other end is a host
# too, so the root port stays empty; what this shows is the controller
# coming up, its rings and its interrupt, and the way back.  Anything that
# hangs is the image's bootwd's to end (nextboot=bootloader).
PATH=/sbin:/usr/sbin:/bin:/usr/bin
export PATH
N=${1:-20}
ADDR=${2:-10.42.0.2/24}
LOG=/root/usbhost-probe.log
exec > $LOG 2>&1
trap '' HUP

echo "probe: start `date`, $N s as a host"
sleep 3
minix-service down dwc3_ecm_0
echo "probe: dwc3_ecm down: $?"
sleep 1
minix-service up /service/xhci -label usbd -args "instance=0 log=3"
echo "probe: xhci up: $?"
sleep "$N"
minix-service down usbd
echo "probe: xhci down: $?"
sleep 1
minix-service up /service/dwc3_ecm -label dwc3_ecm_0 \
    -args "instance=0 log=2"
echo "probe: dwc3_ecm up: $?"
netif=
for t in 1 2 3 4 5 6 7 8 9 10
do
	for i in `ifconfig -l 2>/dev/null`
	do
		[ "$i" != lo0 ] && netif=$i
	done
	[ -n "$netif" ] && break
	sleep 1
done
if [ -n "$netif" ]
then
	ifconfig "$netif" inet "$ADDR" up
	echo "probe: $netif $ADDR: $?"
else
	echo "probe: no interface came back"
fi
echo "probe: end `date`"
