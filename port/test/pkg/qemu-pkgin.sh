#!/bin/bash
# Стенд pkgin на QEMU (этап 12.1).
#
#   bash port/test/pkg/qemu-pkgin.sh
#
# Репозиторий - пакеты из ~/obj-evbarm64/pkg/All (рецепты port/pkg/,
# нужны pkgin и lua) плюс пробный luahello, которому нужен lua (@pkgdep),
# чтобы проверить счёт зависимостей. Пробный репозиторий живёт в своём
# каталоге, в настоящий luahello не попадает. Раздаёт его python3 -m
# http.server на :8000 (гостю - 10.0.2.2), корень на диске, WORK_DIR -
# work-o47. Итог - "qemu-run rc=0": каждая команда вышла с нулём.
set -e
P=/home/minix/obj-evbarm64/pkg
T=/home/minix/obj-evbarm64/pkg-o47test
PORT=$(cd "$(dirname "$0")/../.." && pwd)
pkgin=$(cd $P/All && ls pkgin-*.tgz | tail -1)
rm -rf $T; mkdir -p $T/All $T/stage/bin
cp $P/All/*.tgz $T/All/
printf '#!/usr/pkg/bin/lua\nprint("hello from " .. _VERSION)\n' > $T/stage/bin/luahello
chmod 755 $T/stage/bin/luahello
sh $PORT/mkpkg.sh -n luahello-1.0 -c "Test package that needs lua" \
    -P misc/luahello -D 'lua>=5.4' $T/stage $T/All/luahello-1.0.tgz
python3 $PORT/pkg-summary.py $T/All
cd $T
python3 -m http.server 8000 > $T/http.log 2>&1 &
srv=$!
trap 'kill $srv' EXIT
sleep 1
R=http://10.0.2.2:8000/All
B=/usr/pkg/bin
set +e
QEMU_RUN_LOG=$T/console.log \
python3 $PORT/test/qemu-run.py -w work-o47 -t 300 \
	"pkg_add $R/$pkgin" \
	"mkdir -p /usr/pkg/etc/pkgin && echo $R > /usr/pkg/etc/pkgin/repositories.conf" \
	"$B/pkgin -y update" \
	"$B/pkgin avail; $B/pkgin search lua" \
	"$B/pkgin -y install luahello" \
	"$B/luahello && $B/lua -e 'print(2^10)' && pkg_info" \
	"$B/pkgin show-deps luahello; $B/pkgin show-rev-deps lua" \
	"$B/pkgin -y remove lua && pkg_info && ! test -e $B/lua" \
	"$B/pkgin -y install luahello && $B/pkgin -y remove luahello && $B/pkgin -y autoremove && pkg_info && ! test -e $B/lua" \
	"$B/pkgin -y update"
echo "qemu-run rc=$?"
