#!/bin/bash
# Перенести каталог из NetBSD-current в дерево MEECHO целиком.
#	import.sh external/historical/nawk [...]
#	NETBSD=~/gfxport/nb-src import.sh external/mit/expat
set -uo pipefail
PORT=$(cd "$(dirname "$0")" && pwd)
SRCDIR=${SRCDIR:-$(cd "$PORT/.." && pwd)}

C=${NETBSD:-$HOME/netbsd-current}
M=$SRCDIR
for d in "$@"; do
	if [ ! -d "$C/$d" ]; then echo "нет $C/$d"; exit 1; fi
	rm -rf "${M:?}/$d"
	mkdir -p "$(dirname "$M/$d")"
	cp -a "$C/$d" "$M/$d"
	# Время файлов - время переноса, как после git checkout: с временем
	# клона заголовок, который прежняя сборка поставила в DESTDIR позже,
	# includes счёл бы свежим (так было с expat.h 2.1.0).
	find "$M/$d" -type f -exec touch {} +
	echo "перенесён $d"
done
