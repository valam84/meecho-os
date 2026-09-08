#!/bin/sh
# Прогнать хостовой стенд обслуживания кэшей.
#
# Проверяется разбиение диапазона на строки кэша: какая строка какую
# операцию получает. Это единственная часть, которую больше негде проверить —
# QEMU кэши не моделирует, а на плате ошибка выглядит как редкая порча памяти
# у соседа по строке.
set -e
PORT=$(cd "$(dirname "$0")/../.." && pwd)
SRCDIR=${SRCDIR:-$(cd "$PORT/.." && pwd)}

HERE=${HERE:-$(cd "$(dirname "$0")" && pwd)}
TREE=${TREE:-$SRCDIR}
SRC=$TREE/minix/kernel/arch/aarch64
OUT=${OUT:-/tmp}

CC=${CC:-gcc}
FLAGS="-std=gnu99 -Wall -Wextra -O1 -g -I $SRC -I $TREE/minix/include"

$CC $FLAGS "$HERE/cachetest.c" -o "$OUT/cachetest"
exec "$OUT/cachetest" "$@"
