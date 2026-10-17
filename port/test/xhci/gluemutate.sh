#!/bin/bash
# Mutations of the xHCI glue that gluetrace.sh must catch: each is applied
# to a copy of the driver (or of dwc3_ecm, whose files the Qualcomm glue
# borrows), and the stand on that copy has to fail.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
TREE=$(cd "$HERE/../../.." && pwd)
W=${TMPDIR:-/tmp}/xhci-gluemutate
caught=0 missed=0

mutate() {	# name dir(xhci|dwc3_ecm) file sed-expression
	local src
	case $2 in
	xhci)	  src=$TREE/minix/drivers/usb/xhci ;;
	dwc3_ecm) src=$TREE/minix/drivers/net/dwc3_ecm ;;
	esac
	rm -rf "$W"
	mkdir -p "$W"
	cp -r "$TREE/minix/drivers/usb/xhci" "$W/xhci"
	cp -r "$TREE/minix/drivers/net/dwc3_ecm" "$W/dwc3_ecm"
	sed -i "$4" "$W/$2/$3"
	if cmp -s "$src/$3" "$W/$2/$3"; then
		echo "BAD   $1: the mutation changed nothing"
		missed=$((missed + 1))
		return
	fi
	if XHCI_SRC=$W/xhci DWC3_SRC=$W/dwc3_ecm TMPDIR=$W \
	    bash "$HERE/gluetrace.sh" > "$W.log" 2>&1; then
		echo "MISS  $1"
		missed=$((missed + 1))
	else
		echo "ok    $1 ($(grep -m1 -E 'FAIL|DIFFERENT|error' "$W.log" |
		    sed 's/^ *//' | cut -c1-60))"
		caught=$((caught + 1))
	fi
}

mutate "PHY interface forced 16 bits" xhci xhci_dwc3.c \
    's/if (xhci.info.phy_type == XHCI_PHY_UTMI_WIDE) {/if (1) {/'
mutate "freeclk quirk ignored" xhci xhci_dwc3.c \
    's/if (xhci.info.quirks \& XHCI_Q_DIS_U2_FREECLK)/if (0)/'
mutate "SUSPHY cleared whatever the tree says" xhci xhci_dwc3.c \
    's/if (xhci.info.quirks \& XHCI_Q_DIS_U2_SUSPHY)/if (1)/'
mutate "peripheral taken as a host" xhci xhci_find.c \
    's/if (strcmp(info->dr_mode, "peripheral") == 0) {/if (0) {/'
mutate "device events left armed" xhci xhci_qcom.c \
    's/xhci_rd(xhci.regs, DWC3_GEVNTSIZ0) | DWC3_GEVNTSIZ_INTMASK);/xhci_rd(xhci.regs, DWC3_GEVNTSIZ0));/'
mutate "override left on" xhci xhci_qcom.c \
    's/	if (dt.glue->host != NULL)/	if (0)/'
mutate "core left in host mode" xhci xhci_qcom.c \
    's/	xhci_dwc3_set_mode(DWC3_GCTL_PRTCAPDIR_DEVICE);/	;/'
mutate "taken over with the PLL unlocked" dwc3_ecm qusb2.c \
    's/return (v \& QUSB2_PLL_LOCKED) \&\& /return 1 \&\& /'
mutate "taken over with the domain off" dwc3_ecm dwc3_qcom.c \
    's/	if (!pd_on || held || !clk_on) {/	if (0) {/'
mutate "rockchip clock gate skipped" xhci xhci_rk.c \
    's/	rk_clocks_on();/	;/'
echo "gluemutate: $caught caught, $missed missed"
[ "$missed" -eq 0 ]
