#!/bin/bash
# Mutations of qcom_rpm/glink.c that rpmtest must catch: each one breaks a
# rule taken from Linux (qcom_glink_rpm.c, qcom_glink_native.c, smd-rpm.c).
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$HERE/../../../minix/drivers/power/qcom_rpm
T=${TMPDIR:-/tmp}/rpmmut
rm -rf "$T"; mkdir -p "$T"
rc=0
mut() {
	cp "$SRC/glink.h" "$T/"
	sed "$2" "$SRC/glink.c" > "$T/glink.c"
	if cmp -s "$SRC/glink.c" "$T/glink.c"; then
		echo "$1: the mutation did not apply"; rc=1; return
	fi
	cc -std=c99 -w -O1 -I"$T" -o "$T/t" "$HERE/rpmtest.c" "$T/glink.c" ||
	    { echo "$1: does not build"; rc=1; return; }
	if timeout 20 "$T/t" >/dev/null 2>&1; then
		echo "$1: NOT CAUGHT"; rc=1
	else
		echo "$1: caught"
	fi
}
mut "no index reset"     's/wr(g, g->tx_head, 0);/(void)0;/'
mut "pad to 4, not 8"    's/uint32_t need = ALIGN8(hlen + dlen)/uint32_t need = ALIGN4(hlen + dlen)/'
mut "msg_id from 0"      's/static uint32_t msg_id = 1;/static uint32_t msg_id = 0;/'
mut "no remote open ack" 's/return send_remote_ack(g);$/return GLINK_OK;/'
mut "TX_DATA advance 4"  's/rx_advance(g, ALIGN8(16 + chunk));/rx_advance(g, ALIGN4(16 + chunk));/'
mut "err ignored"        's/r = strcmp(g->rpm_err, "resource does not exist") == 0 ?/r = 0 ? /'
exit $rc
