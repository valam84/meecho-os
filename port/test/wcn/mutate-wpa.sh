#!/bin/sh
# Mutants of minix/drivers/net/wcn/wcn_wpa.c: each breaks one rule of
# 802.11-2016 12.7, and wpatest must fail on it.
#   kckkek      KCK and KEK swapped
#   mickey      MIC keyed with the whole PTK, not the KCK
#   tkslice     TK taken from the wrong part of the PTK
#   unwrapkck   key data unwrapped with the KCK
#   micnohdr    MIC without the 802.1X header
#   nomic       MIC compared and the result ignored
#   noreplay    replay counter not checked
#   replayeq    an equal replay counter accepted
#   noanonce    ANonce of message 3 not compared with message 1's
#   noie        RSNE of message 3 not compared with the beacon's
#   nominmax    PTK data without the min/max ordering
#   prfcount    PRF counter starting at 1
#   rounds      PBKDF2 with 4095 rounds
#   nopad       padding after 0xdd not checked for zeros
#   nosecure    message 3 accepted without Secure
#   noinstall   message 3 accepted without Install
#   reinstall   a retransmitted message 3 gives the PTK again (KRACK)
#   gtkagain    the installed GTK given again
#   earlyrc     replay counter taken before the MIC is checked
#   nopmf       an AP requiring PMF accepted
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$HERE/../../../minix/drivers/net/wcn
OUT=${OUT:-/tmp/wcn-stand}
mkdir -p "$OUT/mut"
CC=${CC:-cc}
CFLAGS="-O1 -g -Wall -Wextra -fsanitize=address,undefined -I$SRC"
fail=0
n=0

mutant() {
	name=$1; shift
	sed "$@" "$SRC/wcn_wpa.c" > "$OUT/mut/wcn_wpa-$name.c"
	if cmp -s "$SRC/wcn_wpa.c" "$OUT/mut/wcn_wpa-$name.c"; then
		echo "mutant $name: the edit did not apply"; fail=1; return
	fi
	if ! $CC $CFLAGS -o "$OUT/mut/wpatest-$name" "$HERE/wpatest.c" \
	    "$OUT/mut/wcn_wpa-$name.c" -lcrypto > "$OUT/mut/$name.cc" 2>&1; then
		echo "mutant $name: does not compile"; fail=1; return
	fi
	n=$((n + 1))
	if "$OUT/mut/wpatest-$name" > "$OUT/mut/$name.txt" 2>&1; then
		echo "MUTANT $name NOT CAUGHT"; fail=1
	else
		echo "mutant $name: caught ($(grep -c FAIL "$OUT/mut/$name.txt")" \
		    "failures) - $(grep FAIL "$OUT/mut/$name.txt" | head -n 1 |
		    sed 's/^FAIL [^ ]* //')"
	fi
}

mutant kckkek -e 's/define KCK(p)\t((p))/define KCK(p)\t((p) + WPA_KCK_LEN)/' \
    -e 's/define KEK(p)\t((p) + WPA_KCK_LEN)/define KEK(p)\t((p))/'
mutant mickey -e 's/cr_hmac_sha1(KCK(ptk), WPA_KCK_LEN,/cr_hmac_sha1(KCK(ptk), WPA_PTK_LEN,/'
mutant tkslice -e 's/define TK(p)\t((p) + WPA_KCK_LEN + WPA_KEK_LEN)/define TK(p)\t((p) + WPA_KCK_LEN)/'
mutant unwrapkck -e 's/cr_unwrap(KEK(ptk),/cr_unwrap(KCK(ptk),/'
mutant micnohdr -e 's/v\[0\]\.p = f;/v[0].p = f + 4;/' -e 's/v\[0\]\.n = O_MIC;/v[0].n = O_MIC - 4;/'
mutant nomic -e 's/if (CRYPTO_memcmp(m, k->mic, MIC_LEN) != 0)/if (0)/'
mutant noreplay -e 's/return sm->have_rc && k->rc <= sm->rc;/return 0;/'
mutant replayeq -e 's/return sm->have_rc && k->rc <= sm->rc;/return sm->have_rc \&\& k->rc < sm->rc;/'
mutant noanonce -e 's/if (memcmp(k->nonce, sm->anonce, WPA_NONCE_LEN) != 0)/if (0)/'
mutant noie -e 's/memcmp(d + off, sm->ap_ie, 2 + l) != 0)/0)/'
mutant nominmax -e 's/MINMAX(aa, spa, 6, lo, hi);/lo = aa; hi = spa;/'
mutant prfcount -e 's/for (i = 0, done = 0; done < outlen; i++, done += k)/for (i = 1, done = 0; done < outlen; i++, done += k)/'
mutant rounds -e 's/define PBKDF2_ROUNDS\t4096/define PBKDF2_ROUNDS\t4095/'
mutant nopad -e 's/if (d\[i\] != 0)/if (0)/'
mutant nosecure -e 's/(KI_INSTALL | KI_SECURE | KI_ENC)/(KI_INSTALL | KI_ENC)/g'
mutant noinstall -e 's/(KI_INSTALL | KI_SECURE | KI_ENC)/(KI_SECURE | KI_ENC)/g'
mutant reinstall -e 's/again = 1;/again = 0;/'
mutant gtkagain -e 's/if (sm->have_gtk && sm->gtk_idx == g->idx/if (0 \&\& sm->gtk_idx == g->idx/'
mutant earlyrc -e '/if ((r = mic_ok(ptk, k)) != 0)/i sm->rc = k->rc; sm->have_rc = 1;'
mutant nopmf -e 's/if (r.mfpr)/if (0)/'
echo "mutants: $n built"
exit $fail
