#!/bin/sh
# pkgin - менеджер двоичных пакетов pkgsrc поверх pkg_install базы: читает
# pkg_summary(5) репозитория в свою базу SQLite, считает зависимости и
# зовёт pkg_add/pkg_delete. Всё, на чём он стоит, уже есть в базе MEECHO:
# libfetch (с SSL), libarchive, sqlite3 (3.53 из NetBSD-current), и сам
# pkg_install в /usr/sbin, как у NetBSD.
#
# libarchive в базе - 2.8 (О45). Свежий pkgin это знает сам: под
# ARCHIVE_VERSION_NUMBER < 3000000 он зовёт support_compression_all
# вместо support_filter_all, и больше от libarchive ему ничего не нужно
# (pkg_summary - сырой поток gzip/bzip2/xz). Импорт libarchive ради pkgin
# поэтому не нужен.
. "$(dirname "$0")/../env.sh"

V=26.4.0
pkg_fetch "https://github.com/NetBSDfr/pkgin/archive/refs/tags/v$V.tar.gz" \
    "pkgin-$V.tar.gz"
pkg_extract "pkgin-$V.tar.gz"

# config.sub пакета 2018 года триплета aarch64-elf64-minix не знает;
# configure нужен только как кросс-сборка, поэтому --host с ОС, которую он
# понимает, и cross_compiling=yes. Все ответы о системе он всё равно
# получает пробами компилятора с sysroot на DESTDIR.
#
# Порядок библиотек - статический: libfetch тянет libssl/libcrypto,
# libarchive - zlib, bzip2 и xz, sqlite3 - libm. Режим сопровождающего у
# pkgin включён по умолчанию и добавляет -Werror: чужой код с новым
# компилятором так не собирать.
./configure --host=aarch64-unknown-netbsd --build="$(./config.guess)" \
    --prefix=/usr/pkg --sysconfdir=/usr/pkg/etc --mandir=/usr/pkg/man \
    --with-dbdir=/var/db/pkgin \
    --with-machine-arch=aarch64 \
    --with-pkg-install=/usr/sbin \
    --disable-maintainer-mode \
    LIBS="-lssl -lcrypto -lbz2 -llzma -lz -lutil -lm" \
    CFLAGS="$CFLAGS -std=gnu99"
make -j"$JOBS"

stage=$(pkg_stage "pkgin-$V")
make install DESTDIR="$stage/.root"
# Staging у mkpkg.sh - то, что ляжет под префикс.
mv "$stage/.root/usr/pkg/"* "$stage/"
rm -rf "$stage/.root"
# Образцы настроек - как у pkgsrc, в share/examples. Сам
# etc/pkgin/repositories.conf пакету не принадлежит: адрес репозитория у
# QEMU и у платы разный, его кладёт корень (mkroot.sh, рядом с
# pkg_install.conf) или человек, и pkg_delete pkgin не должен его уносить.
mkdir -p "$stage/share/examples/pkgin"
cp repositories.conf preferred.conf "$stage/share/examples/pkgin/"

pkg_finish "pkgin-$V" "$stage" \
    -c "Apt / yum like tool for managing pkgsrc binary packages" \
    -d "$RECIPE/DESCR" -l modified-bsd -P pkgtools/pkgin
