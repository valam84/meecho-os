#!/bin/sh
# Собрать двоичный пакет формата pkgsrc из staging-каталога.
#
#   mkpkg.sh -n имя-версия -c "одна строка" [-d файл-описания] [-l лицензия]
#            [-p префикс] [-P категория/имя] [-D зависимость]... staging out.tgz
#
# Пакет pkgsrc - это обычный tar: сначала +CONTENTS (список файлов и
# метки @name/@cwd), +COMMENT, +DESC, +BUILD_INFO, потом сами файлы с
# путями относительно префикса. Порядок обязателен: pkg_add читает архив
# потоком и хочет увидеть метаданные раньше первого файла. Всё это умеет
# pkg_create(1), но он собран для машины, а пакеты делаются на хосте, и
# формат достаточно прост, чтобы сделать его tar'ом и не тащить ещё один
# хостовой инструмент.
#
# +BUILD_INFO нужен pkg_add для проверки платформы: OPSYS и MACHINE_ARCH
# должны совпасть с тем, что вкомпилировано в него (Minix/aarch64), иначе
# отказ без -f; OS_VERSION при расхождении даёт только предупреждение.
# PKGTOOLS_VERSION - версия pkg_install, которой пакет считается сделанным;
# больше версии установленного pkg_add - отказ, поэтому та же, что в дереве
# (external/bsd/pkg_install/dist/lib/version.h).
#
# Staging-каталог - это то, что окажется под префиксом: staging/bin/lua
# станет /usr/pkg/bin/lua. Пустые каталоги записываются как @pkgdir, иначе
# pkg_delete их не уберёт.
set -eu

name= comment= desc= license= prefix=/usr/pkg pkgpath= deps=
OPSYS=${OPSYS:-Minix}
OS_VERSION=${OS_VERSION:-0.1.0}
MACHINE_ARCH=${MACHINE_ARCH:-aarch64}
PKGTOOLS_VERSION=${PKGTOOLS_VERSION:-20260227}

usage() {
	echo "usage: mkpkg.sh -n name-ver -c comment [-d descfile] [-l license]" >&2
	echo "                [-p prefix] [-P pkgpath] [-D dep]... staging out.tgz" >&2
	exit 2
}

while getopts n:c:d:l:p:P:D: opt; do
	case $opt in
	n) name=$OPTARG ;;
	c) comment=$OPTARG ;;
	d) desc=$OPTARG ;;
	l) license=$OPTARG ;;
	p) prefix=$OPTARG ;;
	P) pkgpath=$OPTARG ;;
	D) deps="$deps $OPTARG" ;;
	*) usage ;;
	esac
done
shift $((OPTIND - 1))
[ $# -eq 2 ] && [ -n "$name" ] && [ -n "$comment" ] || usage
stage=$1 out=$2
[ -d "$stage" ] || { echo "mkpkg.sh: $stage: нет такого каталога" >&2; exit 1; }
case $name in *-[0-9]*) ;; *) echo "mkpkg.sh: имя должно быть вида имя-версия: $name" >&2; exit 1 ;; esac

meta=$(mktemp -d)
trap 'rm -rf "$meta"' EXIT

# +CONTENTS. Файлы - отсортированный список относительных путей; каталоги,
# в которых ничего нет, - @pkgdir. Символические ссылки идут как файлы:
# pkg_add переносит их как есть.
{
	echo "@name $name"
	[ -n "$pkgpath" ] && echo "@comment pkgpath=$pkgpath"
	echo "@cwd $prefix"
	for d in $deps; do echo "@pkgdep $d"; done
	(cd "$stage" && find . \( -type f -o -type l \) | sed 's|^\./||' | LC_ALL=C sort)
	(cd "$stage" && find . -type d -empty | sed 's|^\./||' | LC_ALL=C sort | sed 's|^|@pkgdir |')
} > "$meta/+CONTENTS"
printf '%s\n' "$comment" > "$meta/+COMMENT"
if [ -n "$desc" ]; then
	cp "$desc" "$meta/+DESC"
else
	printf '%s\n' "$comment" > "$meta/+DESC"
fi
{
	echo "OPSYS=$OPSYS"
	echo "OS_VERSION=$OS_VERSION"
	echo "MACHINE_ARCH=$MACHINE_ARCH"
	echo "PKGTOOLS_VERSION=$PKGTOOLS_VERSION"
	[ -n "$license" ] && echo "LICENSE=$license"
	[ -n "$pkgpath" ] && echo "PKGPATH=$pkgpath"
	echo "_PKGSRC_MAKER=mkpkg.sh"
} > "$meta/+BUILD_INFO"

# Порядок в архиве задаётся порядком -T; --no-recursion, чтобы каталог не
# утянул содержимое раньше времени. Владелец - root:wheel, не пользователь
# хоста.
{
	echo "+CONTENTS"; echo "+COMMENT"; echo "+DESC"; echo "+BUILD_INFO"
} > "$meta/order"
# Только файлы: каталоги из @pkgdir pkg_add создаёт сам, а встретив их в
# архиве, жалуется "entries not in PLIST".
grep -v '^@' "$meta/+CONTENTS" >> "$meta/order"

# tar'у нужно одно дерево: метаданные подкладываются в staging и убираются
# после. Копии, а не ссылки: ссылки в staging - это ссылки самого пакета,
# и разыменовывать их tar'у нельзя.
for f in +CONTENTS +COMMENT +DESC +BUILD_INFO; do
	cp "$meta/$f" "$stage/$f"
done
tar --format=ustar --owner=0 --group=0 --numeric-owner --no-recursion \
    -C "$stage" -T "$meta/order" -czf "$out"
rm -f "$stage/+CONTENTS" "$stage/+COMMENT" "$stage/+DESC" "$stage/+BUILD_INFO"

echo "$out: $(grep -vc '^@' "$meta/+CONTENTS") файлов, $(du -h "$out" | cut -f1)"
