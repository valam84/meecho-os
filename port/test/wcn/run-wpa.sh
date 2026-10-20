#!/bin/sh
# Host stand of the WPA2-PSK supplicant (minix/drivers/net/wcn/wcn_wpa.c):
# the 802.11 Annex J vectors and an authenticator of the stand's own
# against the library, then the mutants of mutate-wpa.sh.
# Needs libcrypto's headers (Debian/Ubuntu: libssl-dev).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$HERE/../../../minix/drivers/net/wcn
OUT=${OUT:-/tmp/wcn-stand}
mkdir -p "$OUT"
CC=${CC:-cc}
CFLAGS="-O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined -I$SRC"

if [ ! -f /usr/include/openssl/evp.h ]; then
	echo "no libcrypto headers: sudo apt install libssl-dev"; exit 1
fi
$CC $CFLAGS -o "$OUT/wpatest" "$HERE/wpatest.c" "$SRC/wcn_wpa.c" -lcrypto
if ! "$OUT/wpatest" > "$OUT/wpatest.txt" 2>&1; then
	grep -v '^J\.' "$OUT/wpatest.txt" | head -n 60; exit 1
fi
cat "$OUT/wpatest.txt"
sh "$HERE/mutate-wpa.sh"
