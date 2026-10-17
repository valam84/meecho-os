#!/bin/bash
#
# Does the usb_ecm stand catch anything?  Each mutation below puts one
# defect into a COPY of the driver, and run.sh on that copy has to fail.
# None of these was seen on the board - the driver has not run there yet -
# so they are the ways it would be wrong if it were wrong, each silent or
# misleading there:
#
#   pad        a frame shorter than 60 bytes handed up as it came:
#              libnetdriver drops it, and ARP never answers (the tablet's
#              own side, 2026-10-16)
#   padzero    padded with whatever the last frame left in the buffer
#   zlp        a frame of a whole number of packets sent as it is: the
#              device's receiver waits for the rest and two frames merge
#   waits      the receive without USB_URB_WAITS: xhci holds every other
#              transfer behind it and fails it in five seconds
#   rule       a wait asked for while a sent completion is owed: libusb
#              takes the completion for its answer and panics
#   txbusy     a second frame sent while the first is out - the same
#   langid     the station address asked for in language 0
#   alt        SET_INTERFACE to alternate 0: no bulk pair, nothing to read
#   link       NETWORK_CONNECTION ignored
#   linktell   the link changed and lwip not told
#   gonetell   the device gone and lwip not told
#   macfirst   a device with another address driven under the first one's
#   held       a frame not held until lwip takes it: the next overwrites it
#   split      SPEED_CHANGE in two packets read as two notifications
#   config     the first configuration descriptor read whatever xhci set
#   union      a union the wrong way round refused instead of turned round
#   notesize   a notification asked for in 64 bytes on an endpoint of 8:
#              xHCI ends the transfer only at a short packet, and NETWORK_
#              CONNECTION waits for the next notification
#   mcast      multicast never let through the device's filter
#   deinit     SIGTERM tells xhci while a frame is out
#
#   bash mutate.sh

set -u

HERE=$(cd "$(dirname "$0")" && pwd)
TREE=$(cd "$HERE/../../.." && pwd)
SRC=$TREE/minix/drivers/usb/usb_ecm
W=${TMPDIR:-/tmp}/usbecm-mutate
caught=0 missed=0

mutate() {	# name file perl-expression
	rm -rf "$W"
	cp -r "$SRC" "$W"
	perl -0pi -e "$3" "$W/$2"
	if cmp -s "$SRC/$2" "$W/$2"; then
		echo "BAD   $1: the mutation changed nothing"
		missed=$((missed + 1))
		return
	fi
	if SRC=$W OBJ=$W.obj sh "$HERE/run.sh" > "$W.log" 2>&1; then
		echo "MISS  $1"
		missed=$((missed + 1))
	else
		echo "ok    $1 ($(grep -m1 -E 'FAIL|PANIC|error' "$W.log" |
		    cut -c1-64))"
		caught=$((caught + 1))
	fi
}

mutate pad usb_ecm.c 's/\tif \(len < NDEV_ETH_PACKET_MIN\) \{/\tif (0) {/'
mutate padzero usb_ecm.c \
    's/\t\tmemset\(RX->buffer \+ len, 0, NDEV_ETH_PACKET_MIN - len\);/\t\t;/'
mutate zlp ecm_desc.c 's/\t\treturn len \+ 1;/\t\treturn len;/'
mutate waits usb_ecm.c \
    's/RX->transfer_flags = USB_URB_WAITS;/RX->transfer_flags = 0;/'
mutate rule usb_ecm.c \
    's/kick\(void\)\n\{\n\tif \(ctl_out \|\| tx_out\)\n\t\treturn;\n/kick(void)\n{\n/'
mutate txbusy usb_ecm.c \
    's/\tif \(ctl_out \|\| tx_out\)\n\t\treturn SUSPEND;/\tif (0)\n\t\treturn SUSPEND;/'
mutate langid usb_ecm.c \
    's/\| ecm\.info\.mac_string, ecm\.langid, 255\);/| ecm.info.mac_string, 0, 255);/'
mutate alt usb_ecm.c \
    's/send_ctl\(0x01, REQ_SET_INTERFACE, ecm\.info\.data_alt,/send_ctl(0x01, REQ_SET_INTERFACE, 0,/'
mutate link usb_ecm.c 's/\t\t\tset_link\(note\.connected != 0\);/\t\t\t;/'
mutate linktell usb_ecm.c \
    's/(\t    up \? "up" : "down"\);\n)\tif \(nd_up\)\n\t\tnetdriver_link\(\);\n/$1/'
mutate gonetell usb_ecm.c \
    's/(\tecm\.stage = ST_NONE;\n)\tif \(nd_up\)\n\t\tnetdriver_link\(\);\n/$1/'
mutate macfirst usb_ecm.c \
    's/if \(nd_up && memcmp\(ecm\.mac, nd_mac, sizeof\(nd_mac\)\) != 0\) \{/if (0) {/'
mutate held usb_ecm.c \
    's/\tecm\.rx_ready = 1;\n/\tecm.rx_ready = 1;\n\tecm.rx_wanted = 1;\n/; s/ && !rx_out && !ecm\.rx_ready\)/ \&\& !rx_out)/'
mutate split ecm_desc.c 's/\t\t\tst->split = 1;\n//'
mutate config usb_ecm.c \
    's/\(ecm\.config == 0 \|\| buf\[5\] == ecm\.config\)/1/'
mutate union ecm_desc.c \
    's/\telse if \(slave == info->ctrl_iface\)\n\t\tinfo->data_iface = master;\n//'
mutate notesize usb_ecm.c \
    's/\tunsigned size = ecm\.info\.note_size;/\tunsigned size = NOTE_DATA;/'
mutate mcast usb_ecm.c 's/\t\tf \|= ECM_FILTER_ALL_MULTICAST;\n//'
mutate deinit usb_ecm.c \
    's/if \(usbd_ep != NONE && !ctl_out && !tx_out\) \{/if (usbd_ep != NONE) {/'

echo "mutate: $caught caught, $missed missed"
[ "$missed" -eq 0 ]
