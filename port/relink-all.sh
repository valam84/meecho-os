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
#
# И второе, найденное 2026-09-12: make НЕ перелинковывает программу, чьи
# объектники не менялись, - libc.a не входит в её зависимости (DPADD по
# умолчанию пуст), и готовый двоичный файл "новее своих исходников". До
# этого дня скрипт честно проходил по 322 каталогам и оставлял 75 программ
# со старой libc: tar, ps, sed, sort, find, vi, mfs, procfs, ld.elf_so -
# и awk с longjmp, починенным двумя днями раньше, который так и падал на
# каждом exit. Поэтому сначала удаляются все исполняемые файлы в obj
# старше libc.a - и make компонует их заново. Проверка после прогона:
# в DESTDIR не должно остаться ELF старше libc.a (хвост вывода).
set -uo pipefail
PORT=$(cd "$(dirname "$0")" && pwd)
SRCDIR=${SRCDIR:-$(cd "$PORT/.." && pwd)}

M=$SRCDIR
SURVEY=$HOME/survey/result.txt
LIST=/tmp/relink-list.txt

sed 's/\r$//' $PORT/build-dirs.sh > /tmp/bd.sh

{
	# Все каталоги обзора, а не только те, что были OK в тот день: sed,
	# sort, find, tar, ps, make, sysctl, ifconfig стояли в обзоре как
	# FAIL, были починены на 8.3 - и с тех пор ни одна перелинковка их не
	# трогала (2026-09-12). Те, что не собираются до сих пор, отказывают
	# быстро и попадают в счётчик fail; это дешевле, чем список, который
	# отстаёт от дерева.
	awk '{ print $2 }' "$SURVEY"
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

# Каталоги, которых в обзоре 8.3 не было: импортированы позже или лежат
# глубже, чем ходит цикл выше.
cat >> "$LIST" <<'EOF'
external/historical/nawk
external/bsd/less
external/bsd/file
external/bsd/nvi
external/bsd/mdocml
external/public-domain/xz
usr.bin/gzip
minix/fs/procfs
minix/fs/mfs
minix/fs/pfs
minix/drivers/usb/usb_hub
minix/drivers/usb/usb_storage
minix/drivers/usb/xhci
libexec/ld.elf_so
crypto/external/apache2/openssl/bin
external/bsd/pkg_install/sbin
EOF
sort -u -o "$LIST" "$LIST"
echo "каталогов: $(wc -l < "$LIST")"

OBJ=${OBJ:-$HOME/obj-evbarm64}
DEST=${DESTDIR:-$HOME/dest-evbarm64}
REF=$DEST/usr/lib/libc.a
n=0
while read -r f; do
	case "$f" in *.o|*.a|*.so|*.so.*|*.pico|*.po|*.d|*.sh|*.py) continue ;; esac
	if file "$f" | grep -q 'ELF.*executable'; then rm -f "$f"; n=$((n+1)); fi
done < <(find "$OBJ" -type f -perm -u+x ! -newer "$REF" \
    ! -path "$OBJ/tools/*" ! -path "$OBJ/tests/*")
echo "удалено исполняемых старше libc.a: $n"

# SRCDIR передаётся явно: копия build-dirs.sh лежит в /tmp, а корень
# дерева он вычисляет от своего пути - для копии это выходит "/", и
# каждый каталог отказывает с "chdir bin/ls: Not a directory".
SRCDIR="$M" bash /tmp/bd.sh $(cat "$LIST") > /tmp/relink.log 2>&1
echo "ok:   $(grep -c '^ok' /tmp/relink.log)"
echo "fail: $(grep -c '^FAIL' /tmp/relink.log)"
grep '^FAIL' /tmp/relink.log | head -20
# Что осталось со старой libc. Пусто - значит, готово.
stale=$(cd "$DEST" && find bin sbin usr/bin usr/sbin libexec usr/libexec service \
    -type f ! -newer usr/lib/libc.a 2>/dev/null | while read -r f; do
	file "$f" | grep -q ELF && echo "$f"; done)
if [ -n "$stale" ]; then
	echo "СО СТАРОЙ libc: $(echo "$stale" | wc -l)"
	echo "$stale" | tr '\n' ' '; echo
else
	echo "в DESTDIR нет программ старше libc.a"
fi
echo "=== relink done"
