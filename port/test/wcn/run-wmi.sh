#!/bin/sh
# Host stand of minix/drivers/net/wcn/wcn_wmi.c (WMI-TLV commands and
# events), then its mutants: each takes one rule out of wcn_wmi.c and the
# stand must fail on it.
#   tlvorder  - TLV header written and read tag first (it is length first)
#   noinvert  - FILTER_PROBE_REQ sent as meant (WMI-TLV inverts it)
#   svc32     - SERVICE_READY's map read 32 bits a word (it uses 4)
#   nobound   - a TLV length not checked against what is left
#   rxlen     - MGMT_RX's buf_len not checked against the frame array
#   ssidcount - num_ssids written where num_bssids goes
#   noreorder - INIT without the offload peers RX_FULL_REORDER asks for
#   memcount  - SERVICE_READY's memory request count not checked
#   bundle    - bundled tx status read from the desc_id array (Linux's slip)
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$HERE/../../../minix/drivers/net/wcn
CAPTURE=$HERE/../../clover/pmos-20261019/pass1/wifi/debugfs/wmi_services.txt
OUT=${OUT:-/tmp/wcn-stand}
mkdir -p "$OUT/mut"
CC=${CC:-cc}
CFLAGS="-O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -I$SRC"

$CC $CFLAGS -o "$OUT/wmitest" "$HERE/wmitest.c" "$SRC/wcn_wmi.c"
"$OUT/wmitest" "$CAPTURE"

fail=0
mutant() {
	name=$1; shift
	sed "$@" "$SRC/wcn_wmi.c" > "$OUT/mut/wcn_wmi-$name.c"
	if cmp -s "$SRC/wcn_wmi.c" "$OUT/mut/wcn_wmi-$name.c"; then
		echo "mutant $name: the edit did not apply"; fail=1; return
	fi
	# The mutant is built without -Werror: it may leave a variable unused.
	$CC -O1 -g -fsanitize=address,undefined -I"$SRC" \
	    -o "$OUT/mut/wmitest-$name" "$HERE/wmitest.c" \
	    "$OUT/mut/wcn_wmi-$name.c"
	if ASAN_OPTIONS=detect_leaks=0 "$OUT/mut/wmitest-$name" "$CAPTURE" \
	    > "$OUT/mut/wmi-$name.txt" 2>&1; then
		echo "MUTANT $name NOT CAUGHT"; fail=1
	else
		echo "mutant $name: caught - $( (grep -m 1 FAIL \
		    "$OUT/mut/wmi-$name.txt" || grep -m 1 ERROR \
		    "$OUT/mut/wmi-$name.txt") | sed 's/^FAIL //' | cut -c 1-90)"
	fi
}

mutant tlvorder -e 's/b->p\[at - 4\] = n;/b->p[at - 2] = n;/' \
    -e 's/b->p\[at - 3\] = n >> 8;/b->p[at - 1] = n >> 8;/' \
    -e 's/n = get16(it->p);/n = get16(it->p + 2);/' \
    -e 's/t = get16(it->p + 2);/t = get16(it->p);/'
mutant noinvert -e 's/s->flags ^ WMI_SCAN_FILTER_PROBE_REQ/s->flags/'
mutant svc32 -e 's/s->w\[id \/ 4\] >> (id % 4)/s->w[id \/ 32] >> (id % 32)/'
mutant nobound -e 's/if (n > it->left - WMI_TLV_HDR || n < min_len(t))/if (n < min_len(t))/'
mutant rxlen -e 's/if (get32(h + 16) > nf)/if (0)/'
mutant ssidcount -e 's/put32(b, s->nbssid);.*/put32(b, s->nssid);/'
mutant noreorder -e 's/(flags & WMI_INIT_RX_REORDER))/(0))/'
mutant memcount -e 's/if (k < 0 || r->num_mem_reqs > i)/if (k < 0)/'
mutant bundle -e 's/c\[i\].status = get32(arr\[1\] + 4 \* i);/c[i].status = get32(arr[0] + 4 * i);/'
exit $fail
