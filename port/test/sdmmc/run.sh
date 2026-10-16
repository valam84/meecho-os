#!/bin/sh
# Прогнать хостовые стенды драйвера sdmmc.
#
# Оба слоя, которые можно проверить без железа: карта (её граница с
# контроллером — таблица функций) и блочное устройство (его границы —
# libblockdriver и гранты). Всё, что от MINIX нужно этим двум, подставлено
# заглушками в stubs/; драйвер под стенд не правился.
set -e
PORT=$(cd "$(dirname "$0")/../.." && pwd)
SRCDIR=${SRCDIR:-$(cd "$PORT/.." && pwd)}

HERE=${HERE:-$(cd "$(dirname "$0")" && pwd)}
TREE=${TREE:-$SRCDIR}
SRC=$TREE/minix/drivers/storage/mmc
OUT=${OUT:-/tmp}

CC=${CC:-gcc}
FLAGS="-std=gnu99 -Wall -Wextra -Wno-unused-parameter -O1 -g -I $HERE/stubs -I $SRC"

$CC $FLAGS "$HERE/cardtest.c" "$SRC/sdmmc_card.c" -o "$OUT/sdmmc-cardtest"
# sdmmc.c включается стендом целиком: в нём всё статическое, а стенду нужны
# и sdmmc_transfer(), и таблица part[]. main() уезжает под другое имя.
$CC $FLAGS -Dmain=sdmmc_main "$HERE/blocktest.c" -o "$OUT/sdmmc-blocktest"
# Такты и сбросы: настоящий <minix/fdt.h> и настоящая читалка libsys, потому
# что стенд разбирает настоящее дерево платы (port/cb2-sd/live.dtb).
$CC -I "$HERE/clkstubs" $FLAGS -D__unused="__attribute__((unused))" \
	-DREAL_FDT_H="\"$TREE/minix/include/minix/fdt.h\"" \
	"$HERE/clktest.c" "$SRC/sdmmc_clk.c" "$SRC/sdmmc_clk_rk3568.c" \
	"$SRC/sdmmc_clk_h616.c" "$SRC/sdmmc_clk_sdm660.c" \
	-I "$TREE/minix/drivers/hid/qcom_touch" \
	"$TREE/minix/lib/libsys/fdt.c" -o "$OUT/sdmmc-clktest"
# Выбор контроллера по номеру экземпляра - на том же дереве.
$CC -I "$HERE/clkstubs" $FLAGS -D__unused="__attribute__((unused))" \
	-DREAL_FDT_H="\"$TREE/minix/include/minix/fdt.h\"" \
	"$HERE/hosttest.c" "$SRC/sdmmc_host.c" "$SRC/sdmmc_clk.c" \
	"$SRC/sdmmc_clk_rk3568.c" "$SRC/sdmmc_clk_h616.c" "$SRC/sdmmc_clk_sdm660.c" \
	"$TREE/minix/lib/libsys/fdt.c" -o "$OUT/sdmmc-hosttest"

# Две машины: CB2 (RK3566, дерево снято с платы) и Tanix TX1 (H313, дерево
# mainline, собрано из исходников - port/h313/).
CB2=$PORT/cb2-sd/live.dtb
TX1=$PORT/h313/h313-tanix-tx1.dtb
# Mi Pad 4: дерево mainline под GPL-2.0-only, в дерево не кладётся;
# его собирает port/clover/mkboot.sh. Нет файла - проверки пропускаются.
CLOVER=${CLOVER_DTB:-$HOME/work-clover-flash/sdm660-xiaomi-clover.dtb}

rc=0
"$OUT/sdmmc-cardtest" "$@" || rc=1
echo
"$OUT/sdmmc-blocktest" "$@" || rc=1
echo
"$OUT/sdmmc-clktest" "$CB2" || rc=1
echo
"$OUT/sdmmc-clktest" "$TX1" tx1 || rc=1
echo
"$OUT/sdmmc-hosttest" "$CB2" || rc=1
echo
"$OUT/sdmmc-hosttest" "$TX1" tx1 || rc=1
echo
if [ -f "$CLOVER" ]
then
	"$OUT/sdmmc-clktest" "$CLOVER" clover || rc=1
	echo
	"$OUT/sdmmc-hosttest" "$CLOVER" clover || rc=1
else
	echo "clover: нет $CLOVER - пропущено (port/clover/mkboot.sh)"
fi
exit $rc
