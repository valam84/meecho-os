#!/bin/bash
# Перелинковать всё, что статически носит libc.
#
# Повод: правка в libc (minix_stack_fill: ps_strings в самом конце кадра
# exec). Библиотека статическая, поэтому починенный код попадает в программу
# только при её перелинковке; пока не перелинкован dhcpcd, он строит кадр
# старым кодом, и хук падает как падал. Список каталогов — те, что собрала
# веха 8.3 (~/survey/result.txt), плюс серверы, драйверы и команды MINIX.
# Каждый каталог по отдельности через build-dirs.sh: объектники на месте,
# так что почти везде дело сводится к одной компоновке.
#
# И ровно поэтому - ВНИМАНИЕ, ЧЕГО ЭТОТ СКРИПТ НЕ ДЕЛАЕТ. Перелинковка
# помогает, когда починен КОД внутри libc. Если же изменился ПРОТОТИП в
# заголовке, чинить надо вызывающую сторону: её код уже сгенерирован по
# старому объявлению, и объектник переживёт любую перелинковку.
#
# Так было с ptrace(): прототип исправили с int на long (иначе на LP64
# теряется старшая половина возвращаемого слова), заголовок и libc
# обновили, userland перелинковали - а trace(1) продолжал говорить "Kernel
# magic check failed", потому что mem.o остался собранным по старому
# объявлению и обрезал прочитанное слово до 32 бит. Лечится только
# пересборкой: rm -rf $OBJ/<каталог> и заново. Разбор - PORTING-LOG.md,
# "Три хвоста, подешевевшие от ssh".
set -uo pipefail
PORT=$(cd "$(dirname "$0")" && pwd)
SRCDIR=${SRCDIR:-$(cd "$PORT/.." && pwd)}

M=$SRCDIR
SURVEY=$HOME/survey/result.txt
LIST=/tmp/relink-list.txt

sed 's/\r$//' $PORT/build-dirs.sh > /tmp/bd.sh

{
	awk '$1 == "OK" { print $2 }' "$SURVEY"
	for top in minix/servers minix/drivers/storage minix/drivers/net \
	    minix/drivers/tty minix/drivers/clock minix/drivers/system \
	    minix/commands minix/usr.sbin minix/usr.bin minix/bin \
	    minix/sbin minix/net external/bsd/dhcpcd \
	    crypto/external/bsd/openssh; do
		for d in "$M"/$top/*/ "$M"/$top/; do
			[ -f "$d/Makefile" ] || continue
			case "$d" in
			*/ramdisk/) continue ;;	# собирается ramimage.sh -b
			esac
			echo "${d#$M/}"
		done
	done
	# OpenSSH: libssh статическая, поэтому после правки libc его программы
	# надо перелинковать так же, как всё остальное; каталоги лежат на
	# уровень глубже, чем ходит цикл выше.
	echo crypto/external/bsd/openssh/lib
	echo crypto/external/bsd/openssh/libexec
	echo crypto/external/bsd/openssh/bin
} | sed 's|/$||' | sort -u > "$LIST"

echo "каталогов: $(wc -l < "$LIST")"
# SRCDIR передаётся явно: копия build-dirs.sh лежит в /tmp, а корень
# дерева он вычисляет от своего пути - для копии это выходит "/", и
# каждый каталог отказывает с "chdir bin/ls: Not a directory".
SRCDIR="$M" bash /tmp/bd.sh $(cat "$LIST") > /tmp/relink.log 2>&1
echo "ok:   $(grep -c '^ok' /tmp/relink.log)"
echo "fail: $(grep -c '^FAIL' /tmp/relink.log)"
grep '^FAIL' /tmp/relink.log | head -20
echo "=== relink done"
