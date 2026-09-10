#!/bin/bash
# Собрать динамически слинкованный тест и поставить его в DESTDIR, откуда
# его подберёт корень на диске (releasetools/evbarm64_rootproto.py).
#
# Отдельным скриптом, а не каталогом дерева, потому что весь userland
# собирается статически (LDSTATIC=-static в share/mk/bsd.own.mk), и одна
# программа с другим правилом компоновки - это пока проверка, а не решение.
#
#	bash port/test/dyn/build.sh
#
# Дальше:
#	DISK_FRESH=1 bash ~/bin/ramimage.sh -d -r
#	# /usr/bin/dyntest
#	# LD_BIND_NOW=1 /usr/bin/dyntest
set -eu
X=${X:-/home/minix/xtools-aarch64/bin/aarch64-elf64-minix}
DEST=${DEST:-/home/minix/dest-evbarm64}
HERE=$(cd "$(dirname "$0")" && pwd)

"$X-gcc" -O2 -Wall -o "$HERE/dyntest" "$HERE/dyn.c"

# Динамический он или нет - вопрос не веры, а PT_INTERP.
if ! "$X-readelf" -lW "$HERE/dyntest" | grep -q "program interpreter"; then
	echo "no PT_INTERP: linked statically, the test would prove nothing" >&2
	exit 1
fi
"$X-readelf" -lW "$HERE/dyntest" | sed -n '/program interpreter/s/^ *//p'
"$X-readelf" -dW "$HERE/dyntest" | sed -n '/NEEDED/s/^ *//p'

# Интерпретатор и разделяемая библиотека должны быть в DESTDIR, иначе на
# машине окажется программа без того, чем её запускают.
for f in usr/libexec/ld.elf_so usr/lib/libc.so.12; do
	[ -e "$DEST/$f" ] || { echo "missing $DEST/$f" >&2; exit 1; }
done

install -c -m 755 "$HERE/dyntest" "$DEST/usr/bin/dyntest"
echo "installed $DEST/usr/bin/dyntest"
