#!/bin/sh
# Массовая кросс-сборка pkgsrc под MEECHO: пройти по пакетам, собрать
# сколько выйдет, записать, что и почему не вышло, и выложить собранное в
# репозиторий. Запускается В СБОРОЧНОМ КОНТЕЙНЕРЕ (srv.sh bulk) и
# рассчитан на то, что его никто не трогает днями.
#
#   bulk.sh [список]     - список PKGPATH (cat/name) по строке; без него -
#                          весь pkgsrc, категории в порядке PRIORITY
#
# Состояние - в $S (~/bulk), продолжение с того же места после любого
# прерывания: пакет, у которого уже есть запись, второй раз не берётся.
# Чтобы повторить упавшие - удалить их строки из status.tsv (или файл
# целиком), собранное останется собранным: pkgsrc сам увидит пакеты.
#
#   status.tsv   PKGPATH  ok|fail|depfail|timeout  причина  секунды
#   log/         журнал каждого НЕсобранного пакета (cat_name.log)
#   report.txt   сводка: числа и частые причины; пишется по ходу
#   STOP         создать файл - прогон остановится после текущего пакета
#
# Сборка одного пакета - обычный `bmake package-install`: зависимости
# pkgsrc собирает сам (хостовые - в /usr/pkg контейнера, под цель - в
# cross-root/usr/pkg). Перед сборкой зависимости сверяются со status.tsv:
# если одна из них уже не собралась, пакет помечается depfail без
# попытки - иначе каждый зависящий от perl пересобирал бы perl заново.
# Если упала зависимость, о которой ещё не было записи, виновником
# считается самый глубокий каталог в журнале, и запись получают оба.
set -u

H=/home/minix
PKGSRC=$H/pkgsrc
S=${BULK_DIR:-$H/bulk}
REPO=${REPO:-$H/obj-evbarm64/pkg/All}
TARGET_PKGS=$H/pkgsrc-packages/All
TIMEOUT=${PKG_TIMEOUT:-10800}		# 3 часа на пакет со всем, что он тянет
MIN_FREE_GB=${MIN_FREE_GB:-40}		# диск общий: ниже - остановиться
DIST_MAX_GB=${DIST_MAX_GB:-80}		# потолок DISTDIR, старые - удаляются
PRIORITY="devel textproc sysutils misc archivers shells editors lang \
	security net converters databases math mail www graphics audio \
	time print fonts chat comms filesystems benchmarks games \
	multimedia www wayland x11 wm"
export PATH=/usr/pkg/bin:/usr/pkg/sbin:$PATH

mkdir -p "$S/log" "$S/deps" "$REPO"
touch "$S/status.tsv"

say() { echo "$(date '+%F %T') $*" | tee -a "$S/bulk.log"; }

known() { grep -q "^$1	" "$S/status.tsv"; }
record() {	# record PKGPATH статус причина секунды
	known "$1" && return 0
	printf '%s\t%s\t%s\t%s\n' "$1" "$2" "$3" "$4" >> "$S/status.tsv"
}
failed() { grep -E -q "^$1	(fail|depfail|timeout)	" "$S/status.tsv"; }

# Список: всё, где есть Makefile, категории по приоритету, остальные -
# следом в алфавитном порядке. meta-pkgs - в конце: они только тянут.
make_list() {
	cats=$PRIORITY
	for c in $(cd "$PKGSRC" && ls -d */ | tr -d /); do
		case " $PRIORITY bootstrap cross distfiles doc licenses mk packages \
		    pkglocate regress templates CVS meta-pkgs " in
		*" $c "*) ;; *) cats="$cats $c" ;;
		esac
	done
	cats="$cats meta-pkgs"
	for c in $cats; do
		[ -d "$PKGSRC/$c" ] || continue
		for d in "$PKGSRC/$c"/*/Makefile; do
			[ -f "$d" ] && echo "$c/$(basename "$(dirname "$d")")"
		done
	done | awk '!seen[$0]++'
}

# Причина отказа - одна строка, по которой их можно сгруппировать.
reason() {	# reason журнал
	r=
	# PKG_FAIL_REASON: причина - строка после «...has PKG_FAIL_REASON set:».
	grep -q 'PKG_FAIL_REASON set' "$1" &&
	    r=$(grep -A1 -m1 'PKG_FAIL_REASON set' "$1" | tail -1 | sed 's/^ERROR: //')
	[ -n "$r" ] || r=$(grep -m1 -E '^ERROR: ' "$1" | sed 's/^ERROR: //')
	[ -n "$r" ] || r=$(grep -m1 -E 'error: |Error: |undefined reference|configure: error' "$1" |
	    sed 's/^[^:]*:[0-9:]* //' | cut -c1-160)
	[ -n "$r" ] || r=$(grep -m1 -E 'Error code|failed' "$1" | cut -c1-160)
	echo "${r:-unknown}" | tr '\t' ' '
}

# Самый глубокий пакет, на котором остановился make: bmake печатает
# «stopped making ... in КАТАЛОГ» изнутри наружу, первый - виновник.
culprit() {	# culprit журнал
	grep -o "stopped making \"[^\"]*\" in $PKGSRC/[^/ ]*/[^/ ]*" "$1" |
	    head -1 | sed "s|.* in $PKGSRC/||"
}

# Прямые зависимости пакета (все виды) - из кэша $S/deps.
deps() {	# deps PKGPATH
	f=$S/deps/$(echo "$1" | tr / _)
	[ -f "$f" ] || (cd "$PKGSRC/$1" 2>/dev/null &&
	    bmake show-depends-pkgpaths 2>/dev/null) | sort -u > "$f"
	cat "$f"
}

# Первая упавшая зависимость на любой глубине, обход в ширину.
bad_dep() {	# bad_dep PKGPATH
	seen=" $1 " queue=$(deps "$1")
	while [ -n "$queue" ]; do
		next=
		for d in $queue; do
			case $seen in *" $d "*) continue ;; esac
			seen="$seen$d "
			failed "$d" && { echo "$d"; return; }
			next="$next $(deps "$d")"
		done
		queue=$next
	done
}

publish() {
	n=0
	for f in "$TARGET_PKGS"/*.tgz; do
		[ -f "$f" ] || continue
		b=$(basename "$f")
		if [ ! -f "$REPO/$b" ] || [ "$f" -nt "$REPO/$b" ]; then
			cp -p "$f" "$REPO/$b.tmp" && mv "$REPO/$b.tmp" "$REPO/$b"
			n=$((n + 1))
		fi
	done
	# Сводка для pkgin - тем же pkg_info -X, что у pkgsrc.
	( cd "$REPO" && for f in *.tgz; do pkg_info -X "$f"; done ) \
	    > "$REPO/.pkg_summary" 2>/dev/null
	gzip -9c "$REPO/.pkg_summary" > "$REPO/pkg_summary.gz.tmp" &&
	    mv "$REPO/pkg_summary.gz.tmp" "$REPO/pkg_summary.gz"
	bzip2 -9c "$REPO/.pkg_summary" > "$REPO/pkg_summary.bz2.tmp" &&
	    mv "$REPO/pkg_summary.bz2.tmp" "$REPO/pkg_summary.bz2"
	rm -f "$REPO/.pkg_summary"
	[ "$n" -gt 0 ] && say "репозиторий: +$n, всего $(ls "$REPO"/*.tgz | wc -l)"
}

report() {
	{
		echo "MEECHO: массовая сборка pkgsrc, $(date '+%F %T')"
		echo
		awk -F'\t' '{n[$2]++} END {for (s in n) printf "%-8s %d\n", s, n[s]}' \
		    "$S/status.tsv" | sort
		echo "пакетов в репозитории: $(ls "$REPO"/*.tgz 2>/dev/null | wc -l)"
		echo
		echo "Частые причины (fail и timeout):"
		awk -F'\t' '$2=="fail"||$2=="timeout" {print $3}' "$S/status.tsv" |
		    sed 's/[0-9][0-9.]*/N/g' | sort | uniq -c | sort -rn | head -40
		echo
		echo "Упавшие зависимости, которые держат больше всего (depfail):"
		awk -F'\t' '$2=="depfail" {print $3}' "$S/status.tsv" |
		    sort | uniq -c | sort -rn | head -40
	} > "$S/report.txt.tmp" && mv "$S/report.txt.tmp" "$S/report.txt"
	# Отчёт виден рядом с репозиторием: https://.../bulk/
	mkdir -p "$REPO/../bulk"
	cp "$S/report.txt" "$S/status.tsv" "$REPO/../bulk/" 2>/dev/null
}

trim_distfiles() {
	d=$H/distfiles
	while [ "$(du -s --block-size=1G "$d" 2>/dev/null | cut -f1)" -gt "$DIST_MAX_GB" ]; do
		old=$(find "$d" -type f -printf '%T@ %p\n' | sort -n | head -50 | cut -d' ' -f2-)
		[ -n "$old" ] || break
		echo "$old" | xargs rm -f
	done
}

LIST=${1:-$S/list.txt}
if [ ! -f "$LIST" ]; then
	say "составляю список пакетов"
	make_list > "$S/list.txt"
	LIST=$S/list.txt
fi
say "начало: $(wc -l < "$LIST") пакетов в списке, записей $(wc -l < "$S/status.tsv")"

done_since=0
# Список - через дескриптор 3, а stdin всего, что внутри, - /dev/null:
# bmake и configure читают stdin и съедают строки списка.
exec < /dev/null
while read -r p <&3; do
	[ -n "$p" ] || continue
	[ -f "$S/STOP" ] && { say "STOP"; break; }
	known "$p" && continue
	free=$(df --output=avail --block-size=1G "$H" | tail -1 | tr -d ' ')
	if [ "$free" -lt "$MIN_FREE_GB" ]; then
		say "свободно ${free} ГБ < $MIN_FREE_GB - остановка"; break
	fi

	dir=$PKGSRC/$p
	# Зависимость (на любой глубине) уже не собралась - не пробовать.
	bad=$(bad_dep "$p")
	if [ -n "$bad" ]; then
		record "$p" depfail "$bad" 0
		continue
	fi

	log=$S/log/$(echo "$p" | tr / _).log
	t0=$(date +%s)
	(cd "$dir" && timeout "$TIMEOUT" bmake package-install) > "$log" 2>&1
	rc=$?
	dt=$(( $(date +%s) - t0 ))
	if [ "$rc" -eq 0 ]; then
		record "$p" ok "" "$dt"
		rm -f "$log"
		say "ok      $p (${dt} с)"
		done_since=$((done_since + 1))
	elif [ "$rc" -eq 124 ]; then
		record "$p" timeout "дольше $TIMEOUT с" "$dt"
		say "timeout $p"
	else
		c=$(culprit "$log")
		why=$(reason "$log")
		if [ -n "$c" ] && [ "$c" != "$p" ]; then
			record "$c" fail "$why" "$dt"
			cp "$log" "$S/log/$(echo "$c" | tr / _).log" 2>/dev/null
			record "$p" depfail "$c" "$dt"
			say "depfail $p <- $c: $why"
		else
			record "$p" fail "$why" "$dt"
			say "fail    $p: $why"
		fi
	fi
	# Рабочие каталоги - сразу: диск общий, а пакетов тысячи.
	(cd "$dir" && bmake clean clean-depends) > /dev/null 2>&1
	trim_distfiles
	if [ "$done_since" -ge 20 ]; then publish; done_since=0; fi
	report
done 3< "$LIST"

publish
report
say "конец"
