#!/bin/sh
# Host stand of the HTT layer (minix/drivers/net/wcn/wcn_htt.c), then its
# mutants: each takes away one fact of the wire format, and the stand
# must fail on every one.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$HERE/../../../minix/drivers/net/wcn
OUT=${OUT:-/tmp/wcn-stand}
mkdir -p "$OUT/mut-htt"
CC=${CC:-cc}
CFLAGS="-O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -I$SRC"

$CC $CFLAGS -o "$OUT/htttest" "$HERE/htttest.c" "$SRC/wcn_htt.c"
"$OUT/htttest"

fail=0
mutant() {
	name=$1; shift
	sed "$@" "$SRC/wcn_htt.c" > "$OUT/mut-htt/wcn_htt-$name.c"
	if cmp -s "$SRC/wcn_htt.c" "$OUT/mut-htt/wcn_htt-$name.c"; then
		echo "mutant $name: the edit did not apply"; fail=1; return
	fi
	$CC $CFLAGS -Wno-error -o "$OUT/mut-htt/htttest-$name" \
	    "$HERE/htttest.c" "$OUT/mut-htt/wcn_htt-$name.c"
	if "$OUT/mut-htt/htttest-$name" > "$OUT/mut-htt/$name.txt" 2>&1; then
		echo "MUTANT $name NOT CAUGHT"; fail=1
	else
		echo "mutant $name: caught - $(grep -c FAIL "$OUT/mut-htt/$name.txt") failures"
	fi
}

# a descriptor offset one word off (msdu_end 12 -> 13)
mutant offset -e 's/{ 74, 90, 27, 37, 4, 26, 7, 12, 1, 2 }/{ 74, 90, 27, 37, 4, 26, 7, 13, 1, 2 }/'
# POSTPONED not set: the firmware would send TX_INSPECT
mutant postponed -e 's/ | F1_POSTPONED;/;/'
# the 32-bit in-order record size in the 64-bit form
mutant inord8 -e 's/#define IN_ORD_REC	12/#define IN_ORD_REC	8/'
# RSSI found without the odd-count padding
mutant rssipad -e 's/words = c->count + (c->count \& 1);/words = c->count;/'
# last_msdu read from the common msdu_end's first word
mutant msduend -e 's/v = r32(b + RXD_MSDU_END + 16);/v = r32(b + RXD_MSDU_END + 12);/'
# high half of the fragment address in the wrong place
mutant paddrhi -e 's/w16(p + 28, (unsigned)(paddr >> 32));/w16(p + 26, (unsigned)(paddr >> 32));/'
# the source address always from addr2
mutant sa -e 's/memcpy(sa, p + 16, 6);/memcpy(sa, p + 10, 6);/'
# removal without shifting the probe run back
mutant hdel -e 's/r->hash\[i\] = r->hash\[j\];/break;/'
# reading the peer id from the wrong byte
mutant peerid -e 's/pm->peer_id = r16(m + 2);/pm->peer_id = r16(m + 1);/'
# ring length check off by one: a short RX_IND accepted
mutant rxind -e 's/if (at > len || (len - at) \/ 4 < ri->nranges)/if (at > len || (len - at) \/ 4 + 1 < ri->nranges)/'
exit $fail
