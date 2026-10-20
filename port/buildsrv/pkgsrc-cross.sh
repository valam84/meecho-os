#!/bin/sh
# Настроить pkgsrc на кросс-сборку пакетов MEECHO. Запускается В
# СБОРОЧНОМ КОНТЕЙНЕРЕ (srv.sh), повторный запуск безопасен:
#
#   sh ~/minix-src/port/buildsrv/pkgsrc-cross.sh
#
# Механизм - собственный у pkgsrc (doc/HOWTO-use-crosscompile): родной
# pkgsrc хоста (bmake, pkg_install) и USE_CROSS_COMPILE с переменными
# CROSS_*. Под OPSYS=Minix pkgsrc берёт кросс-инструменты из
# ${TOOLDIR}/bin/${MACHINE_GNU_PLATFORM}-* (mk/tools/tools.Minix.mk).
# MACHINE_GNU_PLATFORM собирается как арх-vendor-opsys, и наш триплет
# aarch64-elf64-minix в эту схему ложится как vendor=elf64 - поэтому
# CROSS_LOWER_VENDOR=elf64, а не переименование тулчейна.
#
# Родной pkgsrc стоит в /usr/pkg КОНТЕЙНЕРА (том $B/native-pkg, его
# монтирует srv.sh), то есть там же, где пакеты будут стоять на MEECHO.
# Тогда пути инструментов, которые пакет вписывает в себя для работы
# (USE_TOOLS+=...:run, строки интерпретатора), у хоста и у цели
# совпадают, как при сборке NetBSD -> NetBSD, - и pkgsrc-runtools.patch
# снимает запрет на такие пакеты. При bootstrap в ~/pkg пути разошлись бы.
#
# TOOLDIR - каталог обёрток: кросс-тулчейн (~/xtools-aarch64[-cxx]) с
# явным --sysroot и install с нашим strip.
#
# Sysroot - CROSS_DESTDIR, но не сам DESTDIR, а его копия ~/cross-root:
# pkgsrc ставит собранные зависимости туда же (CROSS_DESTDIR/usr/pkg, с
# базой пакетов), а buildsrv-sync.sh sysroot заменяет dest-evbarm64
# целиком. Копия обновляется этим же скриптом, /usr/pkg в ней не
# трогается.
set -eu

H=/home/minix
N=/usr/pkg			# родной pkgsrc (том)
TOOLDIR=$H/crosstools
# Тулчейн с C++ (второй проход build-xtools.sh) - если он есть: в нём
# тот же gcc и ещё g++ с libstdc++.
XT=$H/xtools-aarch64/bin
[ -x "$H/xtools-aarch64-cxx/bin/aarch64-elf64-minix-g++" ] && XT=$H/xtools-aarch64-cxx/bin
T=aarch64-elf64-minix
HERE=$(cd "$(dirname "$0")" && pwd)

[ -x "$XT/$T-gcc" ] || { echo "нет $XT/$T-gcc" >&2; exit 1; }
[ -f "$H/dest-evbarm64/usr/include/stddef.h" ] || { echo "нет sysroot" >&2; exit 1; }
[ -w "$N" ] || { echo "$N не смонтирован на запись (srv.sh монтирует том)" >&2; exit 1; }

# Правки pkgsrc под цель с другой ОС (разбор - в самих патчах).
# Накладываются один раз; повторный запуск видит их и идёт дальше.
for P in "$HERE"/pkgsrc-*.patch; do
	if patch -d "$H/pkgsrc" -p0 -N --dry-run -s < "$P" >/dev/null 2>&1; then
		patch -d "$H/pkgsrc" -p0 -N -s < "$P"
		echo "pkgsrc: наложен $(basename "$P")"
	fi
done

if [ ! -x "$N/bin/bmake" ]; then
	echo "==> bootstrap pkgsrc в $N"
	(cd "$H/pkgsrc/bootstrap" && ./bootstrap --unprivileged --prefix "$N" \
	    --workdir /tmp/bootstrap-work --make-jobs "${JOBS:-8}") \
	    > "$H/bootstrap.log" 2>&1 || { tail -20 "$H/bootstrap.log"; exit 1; }
	rm -rf /tmp/bootstrap-work
fi

mkdir -p "$H/cross-root"
rsync -a --delete --exclude=/usr/pkg/ "$H/dest-evbarm64/" "$H/cross-root/"

rm -rf "$TOOLDIR"
mkdir -p "$TOOLDIR/bin"
# binutils - из первого префикса: второй проход (C++) их не ставит и
# берёт оттуда же. Из $XT поверх - только сам компилятор.
for f in "$H/xtools-aarch64/bin"/$T-*; do
	ln -s "$f" "$TOOLDIR/bin/"
done
for f in "$XT"/$T-gcc* "$XT"/$T-g++ "$XT"/$T-c++ "$XT"/$T-cpp "$XT"/$T-gcov*; do
	[ -e "$f" ] && ln -sf "$f" "$TOOLDIR/bin/"
done
# Компиляторы знают sysroot dest-evbarm64 (--with-sysroot при сборке),
# а зависимости pkgsrc ставит в cross-root: обёртки называют его явно.
# Си - ещё и -std=gnu17 по умолчанию: GCC 15 без него говорит на C23, где
# char *getenv(); значит «без аргументов», и старый код не собирается.
# FORCE_C_STD этого не делает: он идёт через cwrappers, а кросс-сборка
# pkgsrc - через прежние обёртки (mk/wrapper/cmd-sink-cross-gcc). Флаг -
# до аргументов пакета: его собственный -std= стоит позже и побеждает.
for c in gcc g++ c++ cpp; do
	[ -x "$XT/$T-$c" ] || continue
	case $c in gcc|cpp) std=-std=gnu17 ;; *) std= ;; esac
	rm -f "$TOOLDIR/bin/$T-$c"
	printf '#!/bin/sh\nexec "%s" --sysroot="%s" %s "$@"\n' \
	    "$XT/$T-$c" "$H/cross-root" "$std" > "$TOOLDIR/bin/$T-$c"
	chmod +x "$TOOLDIR/bin/$T-$c"
done
# install -s хостового install зовёт хостовой strip, а тот aarch64 не
# понимает: свой install - тот же GNU install с нашим strip.
# (--strip-program без -s install печатает предупреждением на каждый файл.)
cat > "$TOOLDIR/bin/$T-install" <<EOF
#!/bin/sh
for a; do
	case \$a in -s|-*s*) case \$a in --*) ;; *)
		exec /usr/bin/install --strip-program="$TOOLDIR/bin/$T-strip" "\$@";;
	esac;; esac
done
exec /usr/bin/install "\$@"
EOF
chmod +x "$TOOLDIR/bin/$T-install"

MK=$N/etc/mk.conf
# Свой кусок - между метками, чтобы повторный запуск его заменял.
sed -i '/^# >>> meecho-cross/,/^# <<< meecho-cross/d' "$MK"
cat >> "$MK" <<EOF
# >>> meecho-cross (port/buildsrv/pkgsrc-cross.sh)
USE_CROSS_COMPILE?=	yes
TOOLDIR=		$TOOLDIR
CROSS_DESTDIR=		$H/cross-root
CROSS_MACHINE_ARCH=	aarch64
CROSS_OPSYS=		Minix
# pkg_add сверяет OS_VERSION с uname -r только предупреждением;
# то же значение, что ставит port/mkpkg.sh.
CROSS_OS_VERSION=	0.1.0
CROSS_OPSYS_VERSION=	000100
CROSS_LOWER_OPSYS=	minix
CROSS_LOWER_VENDOR=	elf64
CROSS_LOWER_OPSYS_VERSUFFIX=
CROSS_LOWER_OS_VARIANT=
CROSS_LOWER_VARIANT_VERSION=
CROSS_OBJECT_FMT=	ELF
# Родной pkgsrc в /usr/pkg - там же, где пакеты на цели: пути
# инструментов времени работы совпадают (pkgsrc-runtools.patch).
TOOLS_CROSS_RUN_SAME_PATHS=	yes
# gcc.mk ищет кросс-компилятор как \${_CC:H}/\${MACHINE_GNU_PLATFORM}-gcc,
# то есть рядом с тем cc, что найден в PATH (/usr/bin), а не в TOOLDIR.
# Отсюда явный CC - только для сборок под цель: хостовые зависимости
# (USE_CROSS_COMPILE=no) остаются на хостовом gcc. cross-libtool-base -
# хостовый пакет с инструментами цели (LIBTOOL_CROSS_COMPILE=yes; его
# libltdl - код цели), ему тоже кросс-компилятор.
.if \${USE_CROSS_COMPILE:U:tl} == "yes" || \${LIBTOOL_CROSS_COMPILE:U:tl} == "yes"
CC=			$TOOLDIR/bin/$T-gcc
CXX=			$TOOLDIR/bin/$T-g++
TOOLS_PLATFORM.install=	$TOOLDIR/bin/$T-install
# Userland MEECHO статический (ld.elf_so работает, но система им не
# пользуется), и пакеты - тоже.
LDFLAGS+=		-static
.endif

# GCC 15 по умолчанию - C23, и старый код с объявлениями без
# прототипа (getenv(), xexit()) перестаёт собираться. Та же политика,
# что у HOST_CFLAGS дерева: чужому коду - gnu17. Это для хостовых
# сборок (cwrappers); сборкам под цель gnu17 даёт обёртка в TOOLDIR.
FORCE_C_STD?=		gnu17

DISTDIR=		$H/distfiles
WRKOBJDIR=		$H/pkgsrc-work
# Пакеты под цель и хостовые инструменты (TOOL_DEPENDS, собранные с
# USE_CROSS_COMPILE=no) - в разные каталоги: в репозиторий MEECHO идёт
# только первый.
.if \${USE_CROSS_COMPILE:U:tl} == "yes"
PACKAGES=		$H/pkgsrc-packages
.else
PACKAGES=		$H/pkgsrc-packages-native
.endif
MAKE_JOBS=		\${JOBS:U8}
FETCH_USING=		curl
SKIP_LICENSE_CHECK=	yes
ALLOW_VULNERABLE_PACKAGES= yes
# <<< meecho-cross
EOF
echo "pkgsrc: кросс-сборка настроена (TOOLDIR $TOOLDIR, компилятор $XT)"
