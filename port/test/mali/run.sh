#!/bin/sh
# Хостовый стенд службы mali: настоящие mali_find.c и mali_rk.c, настоящая
# читалка дерева libsys, настоящие деревья (плата, плата без оверлеев,
# Allwinner H313) и регистры CRU/PMU, снятые с платы под вендорским Linux.
# Драйвер под стенд не правится; отказ проверки - отказ драйвера.
#
#	sh run.sh       проверки
#	sh run.sh -v    и журнал драйвера
set -e
HERE=${HERE:-$(cd "$(dirname "$0")" && pwd)}
PORT=$(cd "$HERE/../.." && pwd)
TREE=${TREE:-$(cd "$PORT/.." && pwd)}
SRC=${SRC:-$TREE/minix/drivers/video/mali}
OUT=${OUT:-/tmp}

CC=${CC:-gcc}
$CC -std=gnu99 -Wall -Wextra -Wno-unused-parameter -O1 -g \
	-I "$HERE/shim" -I "$SRC" -D__unused="__attribute__((unused))" \
	-DREAL_FDT_H="\"$TREE/minix/include/minix/fdt.h\"" \
	"$HERE/malitest.c" "$SRC/mali_find.c" "$SRC/mali_rk.c" \
	"$TREE/minix/lib/libsys/fdt.c" -o "$OUT/mali-test"

"$OUT/mali-test" "$PORT/cb2-display/live.dtb" \
	"$PORT/cb2-sd/rk3566-bigtreetech-cb2.dtb" \
	"$PORT/h313/h313-tanix-tx1.dtb" "$@"
