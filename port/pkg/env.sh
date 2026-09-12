# Окружение рецепта пакета. Подключается из port/pkg/<имя>/build.sh:
#
#   . "$(dirname "$0")/../env.sh"
#
# Рецепт получает кросс-компилятор с sysroot на DESTDIR (то же, чем
# собирается userland и чем board-cc.sh собирает одиночные программы),
# каталог распаковки и staging, и в конце зовёт pkg_finish, который делает
# .tgz в $PKG_REPO - каталоге репозитория, из которого пакеты раздаются
# по HTTP (port/pkg-serve.sh).
#
# Всё статическое, как весь userland порта: динамики в пакетах нет, пока
# её нет в системе.
#
# Переменные: DESTDIR, XTOOLS, PKG_WORK, PKG_REPO, DISTFILES, JOBS.
# Рецепту доступны $RECIPE (его каталог) и $PORT (каталог port/).
set -eu

BHOME=/home/${USER:-minix}
DESTDIR=${DESTDIR:-$BHOME/dest-evbarm64}
XTOOLS=${XTOOLS:-$BHOME/xtools-aarch64/bin}
PKG_WORK=${PKG_WORK:-$BHOME/obj-evbarm64/pkg}
PKG_REPO=${PKG_REPO:-$PKG_WORK/All}
DISTFILES=${DISTFILES:-$BHOME/distfiles}
JOBS=${JOBS:-8}
# Каталог рецепта и каталог port/: рецепт после pkg_extract стоит в чужом
# дереве, и относительные пути от него не работают.
RECIPE=$(cd "$(dirname "$0")" && pwd)
PORT=$(cd "$RECIPE/../.." && pwd)

TARGET=aarch64-elf64-minix
export CC="$XTOOLS/$TARGET-gcc --sysroot=$DESTDIR"
export AR="$XTOOLS/$TARGET-ar"
export RANLIB="$XTOOLS/$TARGET-ranlib"
export STRIP="$XTOOLS/$TARGET-strip"
export CFLAGS=${CFLAGS:--O2}
export LDFLAGS=${LDFLAGS:--static}

mkdir -p "$PKG_WORK" "$PKG_REPO" "$DISTFILES"

# pkg_fetch URL [имя] - скачать дистрибутив в $DISTFILES, если его там нет.
# Сеть в WSL режет часть хостов; если curl не смог, файл можно положить в
# $DISTFILES руками, и рецепт этого не заметит.
pkg_fetch() {
	local url=$1 f=${2:-$(basename "$1")}
	[ -f "$DISTFILES/$f" ] && return 0
	echo "==> fetch $url"
	curl -fsSL -m 120 -o "$DISTFILES/$f.part" "$url"
	mv "$DISTFILES/$f.part" "$DISTFILES/$f"
}

# pkg_extract архив [каталог] - распаковать заново в $PKG_WORK и перейти
# туда. Заново - чтобы рецепт всегда собирал из чистого дерева.
pkg_extract() {
	local f=$1 d=${2:-${1%.tar.gz}}
	d=${d%.tgz}; d=${d%.tar.xz}; d=${d%.tar.bz2}
	rm -rf "$PKG_WORK/$d"
	tar -xf "$DISTFILES/$f" -C "$PKG_WORK"
	cd "$PKG_WORK/$d"
}

# pkg_stage имя-версия - чистый staging-каталог; печатает его путь.
pkg_stage() {
	local s=$PKG_WORK/stage/$1
	rm -rf "$s"
	mkdir -p "$s"
	echo "$s"
}

# pkg_finish имя-версия staging [ключи mkpkg.sh] - собрать пакет в репозиторий.
# Двоичные файлы раздеваются здесь, а не в рецепте: у каждого upstream
# своё мнение об install -s.
pkg_finish() {
	local name=$1 stage=$2
	shift 2
	find "$stage" -type f -perm -u+x -exec sh -c \
	    'file -b "$1" | grep -q "^ELF" && "$STRIP" "$1"' _ {} \;
	sh "$PORT/mkpkg.sh" -n "$name" "$@" "$stage" "$PKG_REPO/$name.tgz"
}
