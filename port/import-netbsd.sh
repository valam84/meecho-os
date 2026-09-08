#!/bin/bash
# Перенести каталог из NetBSD-current в дерево MEECHO целиком.
#	import.sh external/historical/nawk [...]
set -uo pipefail
PORT=$(cd "$(dirname "$0")" && pwd)
SRCDIR=${SRCDIR:-$(cd "$PORT/.." && pwd)}

C=$HOME/netbsd-current
M=$SRCDIR
for d in "$@"; do
	if [ ! -d "$C/$d" ]; then echo "нет $C/$d"; exit 1; fi
	rm -rf "${M:?}/$d"
	mkdir -p "$(dirname "$M/$d")"
	cp -a "$C/$d" "$M/$d"
	echo "перенесён $d"
done
