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
	"$TREE/minix/lib/libsys/fdt.c" -o "$OUT/sdmmc-clktest"
# Выбор контроллера по номеру экземпляра - на том же дереве.
$CC -I "$HERE/clkstubs" $FLAGS -D__unused="__attribute__((unused))" \
	-DREAL_FDT_H="\"$TREE/minix/include/minix/fdt.h\"" \
	"$HERE/hosttest.c" "$SRC/sdmmc_host.c" "$SRC/sdmmc_clk.c" \
	"$SRC/sdmmc_clk_rk3568.c" "$TREE/minix/lib/libsys/fdt.c" \
	-o "$OUT/sdmmc-hosttest"

rc=0
"$OUT/sdmmc-cardtest" "$@" || rc=1
echo
"$OUT/sdmmc-blocktest" "$@" || rc=1
echo
"$OUT/sdmmc-clktest" "$PORT/cb2-sd/live.dtb" || rc=1
echo
"$OUT/sdmmc-hosttest" "$PORT/cb2-sd/live.dtb" || rc=1
exit $rc
