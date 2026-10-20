#!/bin/sh
# Host stand of minix/drivers/net/wcn/wcn_mlme.c (the client's MLME and
# Ethernet <-> native wifi), then mutants of the file itself: each takes
# away one rule, and the stand must fail on it.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$HERE/../../../minix/drivers/net/wcn
OUT=${OUT:-/tmp/wcn-stand}
mkdir -p "$OUT/mlme-mut"
CC=${CC:-cc}
CFLAGS="-O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined -I$SRC"

$CC $CFLAGS -o "$OUT/mlmetest" "$HERE/mlmetest.c" "$SRC/wcn_mlme.c"
"$OUT/mlmetest"

fail=0
mutant() {
	name=$1; shift
	sed "$@" "$SRC/wcn_mlme.c" > "$OUT/mlme-mut/$name.c"
	if cmp -s "$SRC/wcn_mlme.c" "$OUT/mlme-mut/$name.c"; then
		echo "mutant $name: the edit did not apply"; fail=1; return
	fi
	# Mutants may warn (an unused helper); only the original must not.
	if ! $CC $(echo "$CFLAGS" | sed 's/-Werror//') -I"$SRC" \
	    -o "$OUT/mlme-mut/$name" "$HERE/mlmetest.c" \
	    "$OUT/mlme-mut/$name.c" 2>"$OUT/mlme-mut/$name.cc"; then
		echo "mutant $name: does not compile"; fail=1; return
	fi
	if ASAN_OPTIONS=detect_leaks=0 "$OUT/mlme-mut/$name" \
	    > "$OUT/mlme-mut/$name.txt" 2>&1; then
		echo "MUTANT $name NOT CAUGHT"; fail=1
	else
		why=$(grep -m 1 -E 'FAIL|ERROR: AddressSanitizer|runtime error' \
		    "$OUT/mlme-mut/$name.txt" | cut -c 1-90)
		echo "mutant $name: caught - $why"
	fi
}

# Elements and beacons.
mutant iewalk -e 's/if (l > n - off - 2)/if (l > n - off)/'
mutant ssid33 -e 's/e.ssid_len > 32/e.ssid_len > 33/'
mutant rsncount -e 's/if (cnt == 0 || cnt > (n - off) \/ 4)/if (cnt == 0)/'
mutant rsnver -e 's/if (n < 2 || le16(p) != 1)/if (n < 2)/'
mutant dsfirst -e 's/if (e.ds != NULL \&\& wcn_chan_freq(e.ds\[0\]) != 0)/if (0)/'
mutant mfpr -e 's/if (b->pmf_required)/if (0)/'
mutant hidden -e 's/if (b->hidden \&\& !o->hidden) {/if (0) {/'
mutant pair5 -e 's/return (lower - 36) % 8 == 0;/return 1;/'
# Rates and HT.
mutant basic -e 's/if (k == nours)/if (0)/'
mutant htsel -e 's/if (!ht)$/if (0)/'
mutant ht40 -e 's/htc \&= ~(HTC_40 | HTC_SGI40);/;/'
mutant tkipht -e 's/(b->rsn.pairwise \& WCN_BIT(WCN_CIPHER_CCMP))));/1));/'
# Frames built and parsed.
mutant seq -e 's/(uint16_t)((s->seq \& 0xfff) << 4)/(uint16_t)(s->seq \& 0xfff)/'
mutant privacy -e 's/capab = WCN_CAP_ESS | (b->capab \& WCN_CAP_PRIVACY);/capab = WCN_CAP_ESS;/'
mutant aid -e 's/le16(m.body + 4) \& 0x3fff/le16(m.body + 4)/'
mutant aidmax -e 's/r->aid > AID_MAX/r->aid > 0x3fff/'
mutant source -e 's/return memcmp(m->bssid, s->bssid, WCN_ALEN) == 0 \&\&/return 1 ||/'
# Native wifi.
mutant tods -e 's/buf\[1\] = WCN_FC1_TODS |/buf[1] = WCN_FC1_FROMDS |/'
mutant txtunnel -e 's/(type == ETHERTYPE_AARP ||/(0 ||/'
mutant rxtunnel -e 's/type != ETHERTYPE_AARP \&\& type != ETHERTYPE_IPX/1/'
mutant llc8023 -e 's/plen = type;/;/'
mutant qos -e 's/hl += 2;/hl += 0;/'
mutant amsdu -e 's/(f\[hl - 2\] \& 0x80)/0/'
mutant echo -e 's/self != NULL \&\& memcmp(r->sa, self, WCN_ALEN) == 0/0/'
mutant fromds -e '/case WCN_FC1_FROMDS:/,/break;/s/r->sa = f + 16;/r->sa = f + 10;/'
exit $fail
