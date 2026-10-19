#!/bin/bash
# Each mutation of fwimport.c must make run.sh fail; a mutation it passes
# is a blind spot of the stand.
HERE=$(cd "$(dirname "$0")" && pwd)
F=$(cd "$HERE/../../.." && pwd)/minix/usr.sbin/fwimport/fwimport.c
cp "$F" "$F.orig"
trap 'mv "$F.orig" "$F"' EXIT
caught=0 missed=0
while IFS='|' read -r name from to; do
	[ -n "$name" ] || continue
	cp "$F.orig" "$F"
	sed -i "s/$from/$to/" "$F"
	if cmp -s "$F" "$F.orig"; then echo "NOOP   $name"; continue; fi
	if bash "$HERE/run.sh" >/dev/null 2>&1; then
		echo "MISSED $name"; missed=$((missed + 1))
	else
		echo "caught $name"; caught=$((caught + 1))
	fi
done <<'EOF'
lfn-order|base = (ord - 1) \* 13;|base = ord * 13;
short-case|lowb = lowe = 1;|lowb = lowe = 0;
fat12-odd|(c \& 1) ? v >> 4 : v \& 0xfff|v \& 0xfff
fat32-hiclus|(uint32_t)le16(e + 20) << 16|0
fat-lastchunk|size_t chunk = left < cs ? left : cs;|size_t chunk = cs;


e4-index|if (le32(h + 12 + 12 \* (i - 1)) <= lb)|if (1)
e4-hole|memset(blk, 0, e4.bs);|;
e4-tail|size_t chunk = left < e4.bs ? (size_t)left : e4.bs;|size_t chunk = e4.bs;
EOF
echo "$caught caught, $missed missed"
