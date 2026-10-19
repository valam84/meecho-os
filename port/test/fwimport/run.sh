#!/bin/bash
# Host stand for minix/usr.sbin/fwimport: the reader is checked against
# images made by tools that share nothing with it - mkfs.fat and mtools
# for FAT12, FAT16 and FAT32, mke2fs -d for ext4 (extents, htree) - and
# what it copies out is compared with what went in, byte for byte.
#
#	run.sh			(needs gcc, mkfs.fat, mcopy, mke2fs)
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$(cd "$HERE/../../.." && pwd)
T=$(mktemp -d /tmp/fwimport-test.XXXXXX)
trap 'rm -rf "$T"' EXIT
pass=0 fail=0
ok() { pass=$((pass + 1)); echo "ok   $*"; }
bad() { fail=$((fail + 1)); echo "FAIL $*"; }

gcc -O2 -Wall -Wextra -o "$T/fwimport" "$SRC/minix/usr.sbin/fwimport/fwimport.c" ||
    { echo "FAIL build"; exit 1; }

# The tree: names as the modem partition has them (8.3, lower case),
# long names, a nested directory, sizes around cluster edges, many files.
mk_tree() {
	local d=$1 i
	mkdir -p "$d/image/sub/deeper" "$d/verinfo"
	for i in $(seq 0 40); do
		head -c $(( (i * 7919) % 70000 + i )) /dev/urandom \
		    >"$d/image/modem.b$(printf %02d $i)"
	done
	head -c 0 /dev/urandom >"$d/image/empty.bin"
	head -c 4096 /dev/urandom >"$d/image/mba.mbn"
	head -c 2048 /dev/urandom >"$d/image/sub/a512_zap.mdt"
	head -c 65537 /dev/urandom >"$d/image/sub/deeper/A_Long_Mixed_Name.firmware"
	head -c 3000000 /dev/urandom >"$d/image/big.bin"
	echo version >"$d/verinfo/ver_info.txt"
}

# fat SIZE_MB FATBITS
fat_case() {
	local mb=$1 bits=$2 img=$T/fat$2.img tree=$T/tree$2 out=$T/out$2
	rm -rf "$tree" "$out"; mk_tree "$tree"
	[ "$bits" = 12 ] && rm -f "$tree/image/big.bin" "$tree"/image/modem.b[1-4]*
	truncate -s ${mb}M "$img"
	# FAT32 with one-sector clusters and 40 MB in front: the files then
	# sit past cluster 65535, where the high half of the number matters.
	local spc=""
	[ "$bits" = 32 ] && spc="-s 1"
	mkfs.fat -F $bits -S 512 $spc "$img" >/dev/null || { bad "mkfs.fat -F $bits"; return; }
	if [ "$bits" = 32 ]; then
		head -c 41943040 /dev/zero >"$T/fill"
		MTOOLS_SKIP_CHECK=1 mcopy -i "$img" "$T/fill" ::fill
	fi
	MTOOLS_SKIP_CHECK=1 mcopy -s -i "$img" "$tree/image" "$tree/verinfo" :: ||
	    { bad "mcopy FAT$bits"; return; }
	# A name in upper case is stored short, without a long name and
	# without case bits - as a DOS tool writes it - and comes out in lower
	# case, the case the services ask for.
	echo upper >"$T/UPPER.BIN"
	MTOOLS_SKIP_CHECK=1 mcopy -i "$img" "$T/UPPER.BIN" ::IMAGE/UPPER.BIN
	cp "$T/UPPER.BIN" "$tree/image/upper.bin"
	if "$T/fwimport" fat "$img" image "$out" >"$T/log" 2>&1 &&
	    diff -r "$tree/image" "$out" >/dev/null; then
		ok "FAT$bits: image/ copied whole ($(tail -1 "$T/log"))"
	else
		bad "FAT$bits: copy differs"; cat "$T/log"; diff -rq "$tree/image" "$out"
	fi
	if "$T/fwimport" fat "$img" IMAGE/SUB "$out.sub" >/dev/null 2>&1 &&
	    diff -r "$tree/image/sub" "$out.sub" >/dev/null; then
		ok "FAT$bits: a nested directory, named in another case"
	else
		bad "FAT$bits: nested directory"
	fi
	if "$T/fwimport" fat "$img" nonesuch "$out.none" >/dev/null 2>&1; then
		bad "FAT$bits: a missing directory was not refused"
	else
		ok "FAT$bits: a missing directory is refused"
	fi
}
fat_case 8 12
fat_case 64 16
fat_case 300 32

# ext4: a firmware directory as vendor has it, plus a directory big enough
# to be indexed (htree), files with holes and of many extents.
e4tree=$T/e4tree; mkdir -p "$e4tree/firmware/wlan/qca_cld" "$e4tree/big"
for f in a512_zap.b00 a512_zap.b01 a512_zap.b02 a512_zap.mdt a530_pm4.fw a530_pfp.fw; do
	head -c $((RANDOM * 3 + 100)) /dev/urandom >"$e4tree/firmware/$f"
done
for i in $(seq 1 600); do echo $i >"$e4tree/big/file-with-a-long-name-$i"; done
head -c 9000000 /dev/urandom >"$e4tree/big/large.bin"
truncate -s 100M "$e4tree/big/sparse.bin"; echo end >>"$e4tree/big/sparse.bin"
# A block of data every other block: 1200 extents, so an extent tree with
# an index of several leaves.
for i in $(seq 0 1199); do
	head -c 4096 /dev/urandom | dd of="$e4tree/big/holey.bin" bs=4096 \
	    seek=$((2 * i)) conv=notrunc status=none
done
ln -s /nowhere "$e4tree/firmware/wlan/qca_cld/link"
for bits in 32 64; do
	img=$T/e4-$bits.img out=$T/e4out-$bits
	opt=""; [ $bits = 64 ] && opt="-O 64bit"
	mke2fs -q -t ext4 $opt -b 4096 -d "$e4tree" "$img" 256M ||
	    { bad "mke2fs ($bits-bit)"; continue; }
	if "$T/fwimport" ext4 "$img" "$out" /firmware/a530_pm4.fw \
	    /firmware/a530_pfp.fw /firmware/a512_zap.mdt /firmware/a512_zap.b00 \
	    /firmware/a512_zap.b01 /firmware/a512_zap.b02 /big/large.bin \
	    /big/sparse.bin /big/holey.bin /big/file-with-a-long-name-599 \
	    >"$T/log" 2>&1 &&
	    cmp -s "$e4tree/big/holey.bin" "$out/holey.bin" &&
	    cmp -s "$e4tree/firmware/a530_pm4.fw" "$out/a530_pm4.fw" &&
	    cmp -s "$e4tree/firmware/a512_zap.b02" "$out/a512_zap.b02" &&
	    cmp -s "$e4tree/big/large.bin" "$out/large.bin" &&
	    cmp -s "$e4tree/big/sparse.bin" "$out/sparse.bin" &&
	    cmp -s "$e4tree/big/file-with-a-long-name-599" "$out/file-with-a-long-name-599"
	then
		ok "ext4 ($bits-bit): files, a large one, a sparse one, one in an htree directory"
	else
		bad "ext4 ($bits-bit)"; cat "$T/log"
	fi
	if "$T/fwimport" ext4 "$img" "$out" /firmware/nonesuch >/dev/null 2>&1; then
		bad "ext4 ($bits-bit): a missing file was not refused"
	else
		ok "ext4 ($bits-bit): a missing file is refused"
	fi
	if "$T/fwimport" ext4 "$img" "$out" /firmware/wlan/qca_cld/link >/dev/null 2>&1; then
		bad "ext4 ($bits-bit): a symbolic link was copied"
	else
		ok "ext4 ($bits-bit): a symbolic link is refused"
	fi
done

# The real thing, when a copy of the tablet's partitions is at hand:
# FWIMPORT_MODEM=modem.img FWIMPORT_VENDOR=vendor.img run.sh
if [ -n "${FWIMPORT_MODEM:-}" ]; then
	"$T/fwimport" fat "$FWIMPORT_MODEM" image "$T/real" && ok "modem partition: $(ls "$T/real" | wc -l) files" || bad "modem partition"
fi
if [ -n "${FWIMPORT_VENDOR:-}" ]; then
	"$T/fwimport" ext4 "$FWIMPORT_VENDOR" "$T/realv" /firmware/a530_pm4.fw /firmware/a530_pfp.fw && ok "vendor partition" || bad "vendor partition"
fi
echo "$pass passed, $fail failed"
[ $fail -eq 0 ]
