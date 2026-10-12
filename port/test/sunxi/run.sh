#!/bin/sh
# Стенд sunxi: собрать драйвер sdmmc на голый ARMv7 и прогнать его на
# эмулируемом контроллере Allwinner (QEMU bpim2u) с двумя картами:
#
#   64 МиБ  - SDSC: адресация байтами, CSD версии 1
#   4 ГиБ   - SDHC: адресация секторами, CSD версии 2 (файл разреженный)
#
# Нужны arm-none-eabi-gcc с newlib (пакеты gcc-arm-none-eabi и
# libnewlib-arm-none-eabi), qemu-system-arm, dtc, python3.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
TREE=${TREE:-$(cd "$HERE/../../.." && pwd)}
SRC=$TREE/minix/drivers/storage/mmc
OUT=${OUT:-$HOME/obj-sunxi-harness}
mkdir -p "$OUT"

CC=arm-none-eabi-gcc
CFLAGS="-march=armv7-a -marm -mfloat-abi=soft -O1 -g -std=gnu99 -Wall \
	-Wno-unused-function -Wno-unused-variable -Wno-unused-parameter \
	-I $HERE/include -I $SRC \
	-DREAL_FDT_H=\"$TREE/minix/include/minix/fdt.h\" \
	-DREAL_LOG_H=\"$TREE/minix/include/minix/log.h\" \
	-D__unused=__attribute__((unused))"

dtc -q -I dts -O dtb -o "$OUT/qemu-r40.dtb" "$HERE/qemu-r40.dts"

objs=
for f in "$HERE/harness.c" "$SRC/sdmmc_card.c" "$SRC/sdmmc_host.c" \
    "$SRC/sdmmc_clk.c" "$SRC/sdmmc_clk_rk3568.c" "$SRC/sdmmc_clk_h616.c" \
    "$SRC/sdmmc_sunxi.c" "$TREE/minix/lib/libsys/fdt.c"; do
	o=$OUT/$(basename "$f" .c).o
	$CC $CFLAGS -c "$f" -o "$o"
	objs="$objs $o"
done
$CC -march=armv7-a -marm -c -Wa,-I"$OUT" "$HERE/start.S" -o "$OUT/start.o"
$CC -march=armv7-a -marm -mfloat-abi=soft -nostartfiles \
	-T "$HERE/link.ld" -o "$OUT/harness.elf" \
	"$OUT/start.o" $objs -lc -lgcc
echo "built $OUT/harness.elf"

rc=0
for size in 67108864 4294967296; do
	img=$OUT/sd-$size.img
	rm -f "$img"
	python3 "$HERE/mkimg.py" make "$img" $size
	echo "=== card of $size bytes ==="
	if timeout ${QEMU_TIMEOUT:-120} qemu-system-arm -M bpim2u -m 1G \
	    -nographic -monitor none -serial stdio \
	    -semihosting-config enable=on,target=native \
	    -kernel "$OUT/harness.elf" \
	    -drive if=sd,index=0,format=raw,file="$img" \
	    > "$OUT/run-$size.log" 2>&1; then
		:
	else
		echo "qemu exited $?"
		rc=1
	fi
	tr -d '\r' < "$OUT/run-$size.log" | grep -v "^$" | tail -40
	python3 "$HERE/mkimg.py" check "$img" || rc=1
done
exit $rc
