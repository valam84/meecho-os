#!/bin/sh
# Прогнать хостовой стенд драйвера dwmac.
#
# В нём две проверки, и обе — те, которые больше негде сделать: во что
# превращается список выводов, и какой станционный адрес даёт идентификатор
# чипа. QEMU этого железа не знает вовсе, а на плате ошибка в первой
# выглядит как линк, который не поднимается, а во второй — как совершенно
# правдоподобный, но чужой адрес.
set -e
PORT=$(cd "$(dirname "$0")/../.." && pwd)
SRCDIR=${SRCDIR:-$(cd "$PORT/.." && pwd)}

HERE=${HERE:-$(cd "$(dirname "$0")" && pwd)}
TREE=${TREE:-$SRCDIR}
SRC=$TREE/minix/drivers/net/dwmac
OUT=${OUT:-/tmp}

CC=${CC:-gcc}
FLAGS="-std=gnu99 -Wall -Wextra -O1 -g -I $SRC"

rc=0

$CC $FLAGS "$HERE/pintest.c" "$SRC/dwmac_pins.c" -o "$OUT/dwmac-pintest"
"$OUT/dwmac-pintest" "$@" || rc=1

$CC $FLAGS "$HERE/socidtest.c" "$SRC/dwmac_socid.c" -o "$OUT/dwmac-socidtest"
"$OUT/dwmac-socidtest" || rc=1

exit $rc
