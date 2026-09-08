#!/bin/sh
# Собрать драйвер sdmmc и показать первую ошибку, если она есть.
set -u
PORT=$(cd "$(dirname "$0")" && pwd)
SRCDIR=${SRCDIR:-$(cd "$PORT/.." && pwd)}

M=$HOME/tools-evbarm64/bin/nbmake-evbarm64-el
D=minix/drivers/storage/mmc
cd $SRCDIR || exit 1
"$M" -C "$D" obj > /tmp/sdmmc-obj.log 2>&1
echo "obj rc=$?"
"$M" -C "$D" -j8 dependall > /tmp/sdmmc-build.log 2>&1
rc=$?
echo "dependall rc=$rc"
if [ $rc -ne 0 ]; then
	echo "--- first errors ---"
	grep -nE "error:|Error:|\*\*\*" /tmp/sdmmc-build.log | head -40
	echo "--- tail ---"
	tail -25 /tmp/sdmmc-build.log
else
	echo "--- warnings ---"
	grep -nE "warning:" /tmp/sdmmc-build.log | head -30
	ls -l $HOME/obj-evbarm64/minix/drivers/storage/mmc/ 2>/dev/null
fi
