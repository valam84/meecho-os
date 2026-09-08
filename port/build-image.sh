#!/bin/sh
# Собрать ramdisk и загрузочный образ, честно проверив статус.
#
# Файлом, а не строкой: $? внутри wsl -- bash -c '...' раскрывается
# промежуточной оболочкой и всегда читается нулём (см. CLAUDE.md,
# «Подводные камни окружения»).
set -u
PORT=$(cd "$(dirname "$0")" && pwd)
SRCDIR=${SRCDIR:-$(cd "$PORT/.." && pwd)}

bash "$PORT/ramimage.sh" -b > /tmp/ri.log 2>&1
rc=$?
echo "ramimage -b rc=$rc"
if [ $rc -ne 0 ]; then
	grep -nE "error|Error|Stop|don't know how" /tmp/ri.log | head -15
	exit $rc
fi
ls -l $HOME/obj-evbarm64/work/boot.mba
bash "$PORT/mkcard.sh" $PORT/cb2-card > /tmp/mk.log 2>&1
rc=$?
echo "mkcard rc=$rc"
[ $rc -eq 0 ] || tail -10 /tmp/mk.log
exit $rc
