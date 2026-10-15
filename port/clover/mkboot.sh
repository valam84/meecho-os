#!/bin/bash
# boot.img for the Xiaomi Mi Pad 4 (clover), to be started with
# "fastboot boot" - once, from RAM, without writing anything on the tablet;
# the next reboot is LineageOS again.
#
#	mkboot.sh [OUT]			default: $WORK_DIR/boot.img
#
# What goes in:
#   kernel   the kernel (KERNEL_ELF, default the shared one) as a flat
#            image, gzipped, with the clover device tree appended - the
#            "Image.gz-dtb" an ABL expects with a v0 header
#   dtb      sdm660-xiaomi-clover.dts (the LTE model: fastboot says
#            "variant: SDM EMMC"; the Wi-Fi one is sda660-...) from the
#            sdm660-mainline kernel tree in $DTS_TREE - a sparse clone of
#            github.com/sdm660-mainline/linux with arch/arm64/boot/dts/qcom,
#            include/dt-bindings and include/uapi is enough (some bindings
#            are symlinks into uapi)
#   ramdisk  boot.mba, the boot archive evbarm64_ramimage.sh makes
#   cmdline  CMDLINE, default below
#
# The kernel relocates itself (head.S), so where the ABL puts it does not
# matter; it prints to the splash screen the ABL left lit (fbcon.c).
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$(cd "$HERE/../.." && pwd)
: ${WORK_DIR:=$HOME/work-clover}
: ${DTS_TREE:=$HOME/sdm660-linux}
: ${DTS:=sdm660-xiaomi-clover}
# One core, a word per boot step on the screen, and a reset through PSCI
# after ten minutes - which, after "fastboot boot", is LineageOS again.
: ${CMDLINE:=bootramdisk=1 console=tty00 no_smp=1 verbose=3 bootwd=600}
export WORK_DIR
OUT=${1:-$WORK_DIR/boot.img}
mkdir -p "$WORK_DIR"

# 1. kernel.bin and boot.mba, exactly as for QEMU - or, with BOOT_MBA set,
# that archive as it is and only the kernel converted: a machine without the
# userland's object directory can still rebuild and boot the kernel.
if [ -n "${BOOT_MBA:-}" ]; then
	: ${KERNEL_ELF:?KERNEL_ELF must name the kernel when BOOT_MBA is set}
	cp "$BOOT_MBA" "$WORK_DIR/boot.mba" || exit 1
	${OBJCOPY:-aarch64-linux-gnu-objcopy} -O binary "$KERNEL_ELF" \
	    "$WORK_DIR/kernel.bin" || exit 1
else
	bash "$SRC/port/ramimage.sh" > "$WORK_DIR/ramimage.log" 2>&1 ||
	    { echo "FAIL: ramimage.sh ($WORK_DIR/ramimage.log)"; exit 1; }
fi
for f in kernel.bin boot.mba; do
	[ -s "$WORK_DIR/$f" ] || { echo "FAIL: no $WORK_DIR/$f"; exit 1; }
done

# 2. The device tree, through cpp as the kernel build would.
Q=$DTS_TREE/arch/arm64/boot/dts/qcom
[ -f "$Q/$DTS.dts" ] || { echo "FAIL: no $Q/$DTS.dts"; exit 1; }
cpp -nostdinc -undef -D__DTS__ -x assembler-with-cpp \
    -I "$Q" -I "$DTS_TREE/include" "$Q/$DTS.dts" 2>"$WORK_DIR/dtc.log" |
    dtc -q -I dts -O dtb -o "$WORK_DIR/$DTS.dtb" - 2>>"$WORK_DIR/dtc.log" ||
    { echo "FAIL: dtc ($WORK_DIR/dtc.log)"; exit 1; }

# 3. Image.gz-dtb and the boot image.
gzip -9n < "$WORK_DIR/kernel.bin" > "$WORK_DIR/kernel.gz" || exit 1
cat "$WORK_DIR/kernel.gz" "$WORK_DIR/$DTS.dtb" > "$WORK_DIR/kernel.gz-dtb"
python3 "$HERE/mkbootimg.py" --kernel "$WORK_DIR/kernel.gz-dtb" \
    --ramdisk "$WORK_DIR/boot.mba" --cmdline "$CMDLINE" -o "$OUT" || exit 1

echo "ok $OUT ($(stat -c %s "$OUT") bytes)"
echo "dtb $DTS from $(git -C "$DTS_TREE" rev-parse --short HEAD 2>/dev/null)"
md5sum "$OUT" "$WORK_DIR/kernel.bin" "$WORK_DIR/boot.mba" "$WORK_DIR/$DTS.dtb"
