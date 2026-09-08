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

rc=0
"$OUT/sdmmc-cardtest" "$@" || rc=1
echo
"$OUT/sdmmc-blocktest" "$@" || rc=1
exit $rc
