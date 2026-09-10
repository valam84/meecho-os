#!/bin/bash
# Собрать зонд контекста и поставить его в DESTDIR, откуда его подберёт
# корень на диске (releasetools/evbarm64_rootproto.py).
#
#	bash port/test/ctx/build.sh
#
# Дальше:
#	DISK_FRESH=1 bash ~/bin/ramimage.sh -d -r
#	# /usr/bin/fpctx
set -eu
X=${X:-/home/minix/xtools-aarch64/bin/aarch64-elf64-minix}
DEST=${DEST:-/home/minix/dest-evbarm64}
HERE=$(cd "$(dirname "$0")" && pwd)

"$X-gcc" -O2 -Wall -Wextra -o "$HERE/fpctx" "$HERE/fpctx.c"

install -c -m 755 "$HERE/fpctx" "$DEST/usr/bin/fpctx"
echo "installed $DEST/usr/bin/fpctx"
