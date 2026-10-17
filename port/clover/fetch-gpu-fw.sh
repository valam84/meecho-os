#!/bin/sh
# The Adreno 5xx command processor's microcode - a530_pm4.fw (ME) and
# a530_pfp.fw (PFP), which the Mi Pad 4's Adreno 512 runs as Linux does -
# fetched from linux-firmware, never put into this tree (hw-gpu.md, §5).
#
# Their licence, LICENSE.qcom (with NOTICE.qcom), lets them be passed on in
# binary form, for Qualcomm platforms only, with the licence beside them,
# and forbids subjecting them to any open source licence; MEECHO's tree is
# BSD.  So they live outside it, licence and notice with them, laid out as
# in linux-firmware (qcom/...), and a kit built from them is a local one.
#
# Everything comes from one pinned commit and is checked against the sha256
# pinned here: a file that does not match is not kept.
#
#	fetch-gpu-fw.sh [DIR]		(default ~/gpu-fw)
#
# The a530 files have not changed since they were added (f36a8e21,
# 2017-11-15); the commit pinned is linux-firmware's main of 2026-10-16.
# PFP 1.87.01: its words 1 and 3 (0x005ff08a, 0x00087001) say it has
# CP_WHERE_AM_I (Linux's a5xx_ucode_check_version()).
set -eu
COMMIT=12dfe670ce7c5987a60fab36d974c4a7d2a6eeeb
BASE=https://gitlab.com/kernel-firmware/linux-firmware/-/raw/$COMMIT
DIR=${1:-$HOME/gpu-fw}

# path in linux-firmware, path under DIR, sha256
FILES="
qcom/a530_pm4.fw	qcom/a530_pm4.fw	6419f35956ec7307af83723fedfba752520bacd8389eda0d0120e185e4cb1d3f
qcom/a530_pfp.fw	qcom/a530_pfp.fw	7ab3cd917e1f875f6a8387f8bc5efcf11ce9c88542ef2fc3cbda7d4b7b163286
LICENSES/LICENSE.qcom	LICENSE.qcom	be904cd28cb292b80cdb6cf412ab0d9159d431671e987ad433c1f62e0988a9bc
LICENSES/NOTICE.qcom	NOTICE.qcom	fa43e1b9a13b341a07adca9dbe73d0f9072d7966fdfe811c01f0dd2872d7309a
"

mkdir -p "$DIR/qcom"
tmp=$(mktemp -d "$DIR/.fetch.XXXXXX")
trap 'rm -rf "$tmp"' EXIT INT TERM

echo "$FILES" | while read -r src dst sum; do
	[ -n "$src" ] || continue
	f=$tmp/$(basename "$dst")
	if [ -f "$DIR/$dst" ] &&
	    echo "$sum  $DIR/$dst" | sha256sum -c --quiet 2>/dev/null; then
		echo "have  $dst"
		continue
	fi
	curl -sSf --proto =https -m 120 -o "$f" "$BASE/$src"
	if ! echo "$sum  $f" | sha256sum -c --quiet; then
		echo "fetch-gpu-fw: $src does not match its pinned sha256;" \
		    "not kept" >&2
		exit 1
	fi
	mv "$f" "$DIR/$dst"
	echo "got   $dst"
done
echo "fetch-gpu-fw: $DIR - qcom/a530_pm4.fw, qcom/a530_pfp.fw," \
    "LICENSE.qcom, NOTICE.qcom (linux-firmware $COMMIT)"
