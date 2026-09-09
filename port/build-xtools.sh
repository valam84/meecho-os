#!/bin/bash
#
# Сборка внешнего кросс-тулчейна для цели aarch64-elf64-minix.
#
#	bash build-xtools.sh [binutils|gcc|headers|all]
#
# Собирает binutils 2.46 и GCC 15.2 из исходников пакетов Ubuntu
# (binutils-source, gcc-15-source) с патчами из port/toolchain/ и ставит
# результат в ~/xtools-aarch64. Дальше его подхватывает build-minix.sh через
# EXTERNAL_TOOLCHAIN.
#
# Почему не тулчейн из дерева: binutils 2.23.2 и GCC 4.8.5 цели aarch64-minix
# не знают и хостовым GCC 15 сами не собираются. Учить их — значит патчить
# код 2013 года дважды: под возраст и под порт. Современный GCC с маленьким
# описанием цели — это два файла в gcc/config и несколько строк в config.gcc.
# Подробности — port/PORTING-LOG.md, этап 1.
#
# Патчи в port/toolchain/ сняты с git-деревьев в ~/xtools-src, где велась
# разработка: cd ~/xtools-src/gcc-15.2.0 && git add -N . && git diff.
#
# Зависимости (apt): gcc-15-source binutils-source libgmp-dev libmpfr-dev
# libmpc-dev libisl-dev flex bison libzstd-dev
#

set -euo pipefail
PORT=$(cd "$(dirname "$0")" && pwd)
SRCDIR=${SRCDIR:-$(cd "$PORT/.." && pwd)}


WHAT="${1:-all}"

TARGET=aarch64-elf64-minix
PREFIX="${PREFIX:-$HOME/xtools-aarch64}"
SYSROOT="${SYSROOT:-$HOME/dest-evbarm64}"
WORK="${WORK:-$HOME/xtools-build}"
PATCHES="${PATCHES:-$PORT/toolchain}"
JOBS="${JOBS:-$(nproc)}"

BINUTILS_VER=2.46
GCC_VER=15.2.0
BINUTILS_TAR=/usr/src/binutils/binutils-${BINUTILS_VER}.tar.xz
GCC_TAR=/usr/src/gcc-15/gcc-${GCC_VER}.tar.xz

for p in "${PATCHES}/binutils-${BINUTILS_VER}-aarch64-minix.patch" \
	 "${PATCHES}/gcc-${GCC_VER}-aarch64-minix.patch"; do
	[ -f "$p" ] || { echo "нет патча $p" >&2; exit 1; }
done

mkdir -p "${WORK}" "${PREFIX}"
# fixincludes требует, чтобы каталог заголовков в sysroot существовал, даже
# пустой; сам он его не создаёт.
mkdir -p "${SYSROOT}/usr/include"

unpack() {	# unpack <tarball> <dir> <patch>
	local tar="$1" dir="$2" patch="$3"
	if [ ! -d "${WORK}/${dir}" ]; then
		echo "==> распаковка ${tar}"
		tar -C "${WORK}" -xf "${tar}"
		echo "==> патч ${patch}"
		patch -d "${WORK}/${dir}" -p1 < "${patch}"
	fi
}

build_binutils() {
	unpack "${BINUTILS_TAR}" "binutils-${BINUTILS_VER}" \
		"${PATCHES}/binutils-${BINUTILS_VER}-aarch64-minix.patch"
	local bld="${WORK}/build-binutils"
	rm -rf "${bld}"; mkdir -p "${bld}"; cd "${bld}"
	"${WORK}/binutils-${BINUTILS_VER}/configure" \
		--target=${TARGET} --prefix="${PREFIX}" --with-sysroot="${SYSROOT}" \
		--disable-nls --disable-werror \
		--disable-gdb --disable-gdbserver --disable-sim --disable-gprofng \
		--disable-libdecnumber --disable-readline
	make -j"${JOBS}"
	make install
	"${PREFIX}/bin/${TARGET}-ld" -V | grep -q aarch64elf_minix
	echo "==> binutils готов"
}

build_gcc() {
	unpack "${GCC_TAR}" "gcc-${GCC_VER}" \
		"${PATCHES}/gcc-${GCC_VER}-aarch64-minix.patch"
	local bld="${WORK}/build-gcc"
	rm -rf "${bld}"; mkdir -p "${bld}"; cd "${bld}"
	export PATH="${PREFIX}/bin:${PATH}"
	# Только C и статический libgcc: libc ещё нет, её этот компилятор и
	# будет собирать. --without-headers сам по себе не действует, когда
	# задан --with-sysroot: gcc/configure выставляет inhibit_libc только
	# вместе с --with-newlib. Это документированный способ сказать «libc
	# пока нет»; ни на что другое в C-only сборке с нашей записью в
	# libgcc/config.host он не влияет.
	"${WORK}/gcc-${GCC_VER}/configure" \
		--target=${TARGET} --prefix="${PREFIX}" --with-sysroot="${SYSROOT}" \
		--enable-languages=c --disable-shared --disable-threads \
		--disable-nls --disable-multilib --disable-bootstrap \
		--disable-libssp --disable-libquadmath --disable-libatomic \
		--disable-libgomp --disable-libstdcxx \
		--with-gnu-as --with-gnu-ld --without-headers --with-newlib
	make -j"${JOBS}" all-gcc
	make -j"${JOBS}" all-target-libgcc
	make install-gcc install-target-libgcc
	echo 'int x;' | "${PREFIX}/bin/${TARGET}-gcc" -dM -E - | grep -q '__minix'
	echo "==> gcc готов"
}

# GCC creates its private limits.h while the target sysroot is still empty.
# In that bootstrap state it deliberately omits include_next <limits.h>, so
# the compiler never sees such target definitions as SSIZE_MAX and PATH_MAX
# after tree-includes.sh installs them. Recreate the header from the same
# three inputs used by GCC's stmp-int-hdrs rule once the sysroot is populated.
refresh_headers() {
	unpack "${GCC_TAR}" "gcc-${GCC_VER}" \
		"${PATCHES}/gcc-${GCC_VER}-aarch64-minix.patch"
	local gcc="${PREFIX}/bin/${TARGET}-gcc"
	[ -x "$gcc" ] || { echo "нет $gcc — сначала соберите gcc" >&2; exit 1; }
	[ -f "${SYSROOT}/usr/include/limits.h" ] || {
		echo "нет ${SYSROOT}/usr/include/limits.h — сначала установите headers" >&2
		exit 1
	}
	local inc
	inc=$($gcc -print-file-name=include)
	cat "${WORK}/gcc-${GCC_VER}/gcc/limitx.h" \
	    "${WORK}/gcc-${GCC_VER}/gcc/glimits.h" \
	    "${WORK}/gcc-${GCC_VER}/gcc/limity.h" > "${inc}/limits.h"
	echo | "$gcc" -dM -E -include limits.h - | grep -q '^#define SSIZE_MAX '
	echo "==> системные limits.h подключены"
}

case "${WHAT}" in
binutils)	build_binutils ;;
gcc)		build_gcc ;;
headers)	refresh_headers ;;
all)		build_binutils; build_gcc ;;
*)		echo "usage: $0 [binutils|gcc|headers|all]" >&2; exit 1 ;;
esac
