#!/bin/bash
# The Mi Pad 4's kit, everything from the tree as it is: one kernel, one boot
# archive, a root for the userdata partition with the whole userland and the
# desktop, and the boot images that start them (FLASH.md says how to use
# them).
#
#	mkkit.sh OUTDIR
#
# What comes out:
#   meecho-clover-desktop.img  root on eMMC (userdata), the desktop at boot:
#                              meechowm drawn by the Adreno, the on-screen
#                              keyboard, touches; USB hubs, keyboards and
#                              mice behind an OTG adapter (usbrole)
#   meecho-clover-emmc.img     the same root, the console instead of the
#                              desktop (meecho-desktop from the shell)
#   meecho-clover-boot.img     root in memory, all cores, eMMC read-only
#   meecho-clover-safe.img     root in memory, one core, no eMMC driver
#   meecho-clover-root.img.gz  the userdata file system (1 GiB unpacked)
#   SHA256SUMS, cmdline.txt, BUILD.txt
#
# The GPU's firmware is not MEECHO's to ship (hw-gpu.md, §5): the CP's
# microcode comes from linux-firmware under LICENSE.qcom (fetch-gpu-fw.sh),
# the zap shader from this tablet's own vendor partition (vendor-pull.sh).
# FW_DIR names a directory with all of them - a530_pm4.fw, a530_pfp.fw,
# a512_zap.mdt and its .b00-.b02, LICENSE.qcom, NOTICE.qcom - and they go
# into the root as /libdata/firmware/qcom (mkroot.sh, ROOT_EXTRA).  The kit
# is then a local one, not for publication.  FW_DIR=none: a root without
# them - the kit to publish: at its first boot rc.clover copies the GPU's
# files off the tablet's vendor partition and the modem's off its modem
# partition (fwimport), so the tablet runs on its own firmware.
#
# Variables: FW_DIR (required), AUTHKEYS (the key ssh lets in as root; none
# - no ssh login), KVAR (the kernel variant's name, "kit"), EXTRA_CMDLINE
# (appended to every image's command line, e.g. "nextboot=bootloader
# bootwd=1800" for rounds through the hub keeper), DTS_TREE (mkboot.sh).
#
# The shared object directory's work/ is written by ramimage.sh; everything
# this kit needs is copied out of it at once, the rest is built in OUTDIR.
# The images have no bootwd unless EXTRA_CMDLINE gives one: flashed into
# boot, a boot watchdog would reset the tablet for ever.
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$(cd "$HERE/../.." && pwd)
OUT=${1:?OUTDIR}
: ${FW_DIR:?FW_DIR: the GPU firmware (see the head of this script), or none}
: ${KVAR:=kit}
: ${EXTRA_CMDLINE:=}
: ${AUTHKEYS:=/nonexistent}
OBJ=$HOME/obj-evbarm64
mkdir -p "$OUT" && OUT=$(cd "$OUT" && pwd) || exit 1
W=$OUT/work
mkdir -p "$W"
say() { echo "== $*"; }
fail() { echo "FAIL: $*" >&2; exit 1; }

# 0. The firmware, checked before anything is built.
rm -rf "$W/extra"
if [ "$FW_DIR" != none ]; then
	mkdir -p "$W/extra/libdata/firmware/qcom"
	for f in a530_pm4.fw a530_pfp.fw a512_zap.mdt a512_zap.b00 \
	    a512_zap.b01 a512_zap.b02 LICENSE.qcom NOTICE.qcom; do
		[ -s "$FW_DIR/$f" ] || fail "no $FW_DIR/$f"
		cp "$FW_DIR/$f" "$W/extra/libdata/firmware/qcom/" || exit 1
	done
	chmod 644 "$W/extra/libdata/firmware/qcom/"*
fi

# 1. The kernel: -O2, no debug sections, in its own object directory.
say "kernel ($KVAR)"
bash "$SRC/port/kbuild-variant.sh" "$KVAR" "-O2" > "$W/kernel.log" 2>&1 ||
    fail "kernel ($W/kernel.log)"
KELF=$HOME/obj-kvar/$KVAR/minix/kernel/kernel
[ -s "$KELF" ] || fail "no $KELF"
${READELF:-aarch64-linux-gnu-readelf} -S "$KELF" | grep -q '\.debug' &&
    fail "$KELF has debug sections"

# 2. The boot archive: ramdisk and memory rebuilt from DESTDIR.
say "boot archive"
bash "$SRC/port/ramimage.sh" -b > "$W/ramimage.log" 2>&1 ||
    fail "ramimage.sh -b ($W/ramimage.log)"
cp "$OBJ/work/boot.mba" "$W/boot.mba" || exit 1

# 3. The root for userdata.
say "root"
extra=
[ "$FW_DIR" != none ] && extra=$W/extra
( cd "$SRC" && WORK=$W ROOT_NAME=root-clover RC_FILE=port/clover/rc.clover \
    PKG_REPO_URL= AUTHKEYS=$AUTHKEYS ROOT_EXTRA=$extra \
    sh port/mkroot.sh ) > "$W/mkroot.log" 2>&1 || fail "mkroot.sh ($W/mkroot.log)"
cp "$W/root-clover.img.gz" "$OUT/meecho-clover-root.img.gz" || exit 1

# 4. The boot images: one kernel, one archive, four command lines.
# The tablet's own partitions, read only: the modem's storage for rmtfs
# first (rc.clover links the first four for it), then modem and vendor,
# which the firmware is copied off at the first boot (fwimport); modem=1
# starts the modem (hw-modem.md).
EMMC="rootdevname=c0d0 blkdrv=sdmmc gptpart0=userdata console=console osk=1"
EMMC="$EMMC gptro0=modemst1,modemst2,fsg,fsc,modem,vendor modem=1"
: > "$OUT/cmdline.txt"
img() {	# name cmdline
	local cmd="$2${EXTRA_CMDLINE:+ $EXTRA_CMDLINE}"
	say "meecho-clover-$1.img: $cmd"
	WORK_DIR=$W/img-$1 KERNEL_ELF=$KELF BOOT_MBA=$W/boot.mba CMDLINE=$cmd \
	    bash "$HERE/mkboot.sh" "$OUT/meecho-clover-$1.img" \
	    > "$W/mkboot-$1.log" 2>&1 || fail "mkboot.sh ($W/mkboot-$1.log)"
	python3 "$HERE/checkboot.py" "$OUT/meecho-clover-$1.img" \
	    --kernel "$W/img-$1/kernel.bin" --ramdisk "$W/boot.mba" \
	    --dtb "$W/img-$1/sdm660-xiaomi-clover-meecho.dtb" \
	    > "$W/checkboot-$1.log" 2>&1 || fail "checkboot.py ($W/checkboot-$1.log)"
	printf '%-8s %s\n' "$1" "$cmd" >> "$OUT/cmdline.txt"
}
img desktop "$EMMC desktop=1"
img emmc "$EMMC"
img boot "bootramdisk=1 console=console osk=1 blkdrv=sdmmc"
img safe "bootramdisk=1 console=console osk=1 no_smp=1"

# 5. What it is made of, and the sums.
{
	echo "tree   $(git -C "$SRC" log -1 --format='%h %ci %s')"
	git -C "$SRC" status --short | sed 's/^/dirty  /'
	echo "kernel $KELF"
	echo "dtb    $(git -C "${DTS_TREE:-$HOME/sdm660-linux}" rev-parse --short HEAD 2>/dev/null)"
	echo "fw     $FW_DIR"
	echo "keys   $AUTHKEYS"
	echo "built  $(date '+%Y-%m-%d %H:%M')"
} > "$OUT/BUILD.txt"
( cd "$OUT" && sha256sum meecho-clover-*.img meecho-clover-root.img.gz \
    > SHA256SUMS && echo "$(gzip -dc meecho-clover-root.img.gz | sha256sum |
    cut -d' ' -f1)  meecho-clover-root.img" >> SHA256SUMS )
ls -l "$OUT"
say "ok $OUT"
