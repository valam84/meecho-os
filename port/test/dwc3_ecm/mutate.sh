#!/bin/bash
# Mutations of dwc3_ecm that the stand must catch: each is applied to a
# copy of the driver, and run.sh on that copy has to fail.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
TREE=$(cd "$HERE/../../.." && pwd)
SRC=$TREE/minix/drivers/net/dwc3_ecm
W=${TMPDIR:-/tmp}/dwc3-mutate
caught=0 missed=0

mutate() {	# name file sed-expression
	rm -rf "$W"
	cp -r "$SRC" "$W"
	sed -i "$3" "$W/$2"
	if cmp -s "$SRC/$2" "$W/$2"; then
		echo "BAD   $1: the mutation changed nothing"
		missed=$((missed + 1))
		return
	fi
	if DWC3_SRC=$W bash "$HERE/run.sh" > "$W.log" 2>&1; then
		echo "MISS  $1"
		missed=$((missed + 1))
	else
		echo "ok    $1 ($(grep -m1 -E 'FAIL|model:|error' "$W.log" |
		    cut -c1-70))"
		caught=$((caught + 1))
	fi
}

mutate "events not masked first" dwc3_dev.c \
    '/1. No more events/{n;s/^/\/\//}'
mutate "LPM_CAP kept" dwc3_dev.c 's/	    DWC3_DCFG_LPM_CAP);/	    0);/'
mutate "no soft reset" dwc3_dev.c \
    's/(v | DWC3_DCTL_CSFTRST) \& ~DWC3_DCTL_RUN_STOP/v \& ~DWC3_DCTL_RUN_STOP/'
mutate "TRB not cleaned" dwc3_ep0.c 's/	dwc3_give(t, DWC3_LINE);/	(void)t;/'
mutate "events not invalidated" dwc3_dev.c \
    's/	dwc3_take(dwc.ev, DWC3_EVBUF_SIZE);//'
mutate "status types swapped" dwc3_ep0.c \
    's/DWC3_TRBCTL_STATUS3 : DWC3_TRBCTL_STATUS2/DWC3_TRBCTL_STATUS2 : DWC3_TRBCTL_STATUS3/'
mutate "maxpacket stays 512" dwc3_dev.c \
    's/(void)ep_config(0, DWC3_DEPCFG_ACTION_MODIFY, 64);/(void)ep_config(0, DWC3_DEPCFG_ACTION_MODIFY, 512);/'
mutate "address not set" dwc3_ep0.c 's/DWC3_DCFG_DEVADDR(value));/0);/'
mutate "BOS answered" dwc3_ep0.c 's/		return -1;		\/\* BOS among them/		return copy(qual_desc, 5);	\/* BOS among them/'
mutate "PHY lock not required" qusb2.c \
    's/return (v \& QUSB2_PLL_LOCKED) \&\& /return 1 \&\& /'
echo "mutate: $caught caught, $missed missed"
[ "$missed" -eq 0 ]
