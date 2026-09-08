#!/bin/bash
#
# Обёртка над build.sh для сборки MINIX современным хостовым компилятором.
#
#	bash build-minix.sh <arch> [цель...]
#
# Примеры:
#	bash build-minix.sh evbarm64-el tools
#	bash build-minix.sh evbearm-el tools
#	bash build-minix.sh evbarm64-el -j 48 distribution
#
# Каждая архитектура собирается в свой набор каталогов, так что параллельные
# сборки разных целей не мешают друг другу.

set -uo pipefail

ARCH="${1:?укажите архитектуру, например evbarm64-el}"
shift
TARGETS=("$@")
if [ ${#TARGETS[@]} -eq 0 ]; then
	TARGETS=(tools)
fi

SRCDIR="${SRCDIR:-$HOME/minix-src}"
SUFFIX="${ARCH%%-*}"
OBJ="${HOME}/obj-${SUFFIX}"
DEST="${HOME}/dest-${SUFFIX}"
TOOLS="${HOME}/tools-${SUFFIX}"
LOG="${HOME}/build-${SUFFIX}.log"
JOBS="${JOBS:-$(nproc)}"

# ---------------------------------------------------------------------------
# Флаги для хостового компилятора.
#
# Дерево MINIX заморожено на 2018 годе, хостовой GCC здесь 15.x. Два умолчания
# компилятора изменились с тех пор так, что старый код перестаёт собираться:
#
#   -fcommon     GCC 10 переключился на -fno-common. Предварительные
#                определения переменных в заголовках (например
#                usr.bin/make/make.h:443 - "FILE *debug_file;" без extern)
#                стали конфликтом при линковке: одно и то же имя определяется
#                в каждом объектнике, который включил заголовок.
#
#   -std=gnu17   GCC 15 перешёл на C23 по умолчанию. Там bool/true/false -
#                ключевые слова, определения функций в стиле K&R и неявный int
#                удалены из языка. Код 2018 года на этом рассыпается.
#
#   -D_GNU_SOURCE  glibc прячет XSI-функции (wcwidth и подобные) за feature-
#                макросами, а tools/compat/compat_defs.h для Linux наоборот
#                снимает их (#undef _POSIX_SOURCE), выставляя только
#                __USE_ISOC99. В итоге функция в библиотеке есть, а
#                объявления нет. Это стандартный способ собирать
#                BSD-исходники на glibc.
#
# Это флаги совместимости, и они намеренно применяются ТОЛЬКО к хостовым
# утилитам: nbmake, nbconfig и прочий инструментарий, который нужен для
# сборки, но не входит в саму ОС. Код самого MINIX - ядро, серверы, libc -
# чиним по-настоящему, а не флагом: он попадает в поставку, и его мы
# собираемся сопровождать.
# ---------------------------------------------------------------------------
export HOST_CFLAGS="${HOST_CFLAGS:--O -fcommon -std=gnu17 -D_GNU_SOURCE}"

# ---------------------------------------------------------------------------
# Сборка info-документации выключена по умолчанию.
#
# Она тянет makeinfo из texinfo 4.8, который gnu/dist/fetch.sh скачивает
# прямо во время сборки. Это релиз 2004 года, современным компилятором он
# не собирается, а к работе ядра отношения не имеет.
#
# Включается обратно так:  MKINFO=yes bash build-minix.sh ...
# ---------------------------------------------------------------------------
MKINFO="${MKINFO:-no}"

# ---------------------------------------------------------------------------
# Тулчейн для aarch64 — внешний и современный.
#
# Тулчейн в дереве (binutils 2.23.2, GCC 4.8.5, patches/ к ним) цели
# aarch64-minix не знает, и учить его — значит патчить код 2013 года, который
# к тому же сам не собирается хостовым GCC 15. Вместо этого binutils 2.46 и
# GCC 15.2 собираются один раз для цели aarch64-elf64-minix скриптом
# port/build-xtools.sh (патчи — port/toolchain/) и подключаются штатным
# механизмом NetBSD: EXTERNAL_TOOLCHAIN. Это тот же компилятор, что собирает
# ядро в Makefile.bringup, только с MINIX-целью вместо linux-gnu.
#
#   EXTERNAL_TOOLCHAIN   bsd.own.mk берёт ${EXTERNAL_TOOLCHAIN}/bin/<триплет>-*
#   TOOLCHAIN_MISSING    tools/ не собирает binutils и gcc из дерева
#   MKGCC/MKBINUTILS     ничего из external/gpl3 не собирается и для цели
#   MKLLVM/MKLIBCXX      для aarch64 дерево по умолчанию хочет clang; нет
#   MKLIBSTDCXX          иначе HAVE_GCC включает сборку libstdc++ из gcc/dist
#   HAVE_GCC             bsd.sys.mk по нему выбирает флаги предупреждений;
#                        MINIX сам пишет сюда 5 «not really» — схема нумерации
#                        в дереве кончается на 4.8
#
# Sysroot зашит в сам тулчейн при сборке (--with-sysroot=${DEST}), потому что
# ветка EXTERNAL_TOOLCHAIN в bsd.own.mk --sysroot не добавляет.
# ---------------------------------------------------------------------------
XTOOLS_VARS=()
case "${SUFFIX}" in
evbarm64)
	XTOOLS="${XTOOLS:-$HOME/xtools-aarch64}"
	if [ ! -x "${XTOOLS}/bin/aarch64-elf64-minix-gcc" ] && [ "${TARGETS[*]}" != "tools" ]; then
		echo "нет ${XTOOLS}/bin/aarch64-elf64-minix-gcc — сначала bash build-xtools.sh" >&2
		exit 1
	fi
	XTOOLS_VARS=(
		-V EXTERNAL_TOOLCHAIN="${XTOOLS}"
		-V TOOLCHAIN_MISSING=yes
		-V MKGCC=no
		-V MKBINUTILS=no
		-V MKLLVM=no
		-V MKLIBCXX=no
		-V MKLIBSTDCXX=no
		-V HAVE_GCC=15
	)
	;;
esac

echo "=============================================================="
echo " Архитектура : ${ARCH}"
echo " Цели        : ${TARGETS[*]}"
echo " Потоков     : ${JOBS}"
echo " HOST_CFLAGS : ${HOST_CFLAGS}"
echo " MKINFO      : ${MKINFO}"
echo " Тулчейн     : ${XTOOLS:-из дерева}"
echo " Дерево      : ${SRCDIR}"
echo " Лог         : ${LOG}"
echo "=============================================================="

cd "${SRCDIR}" || exit 1

start=$(date +%s)

sh build.sh \
	-j "${JOBS}" \
	-m "${ARCH}" \
	-O "${OBJ}" \
	-D "${DEST}" \
	-T "${TOOLS}" \
	-V HOST_CFLAGS="${HOST_CFLAGS}" \
	-V MKINFO="${MKINFO}" \
	"${XTOOLS_VARS[@]}" \
	-U -u \
	"${TARGETS[@]}" \
	> "${LOG}" 2>&1
rc=$?

elapsed=$(( $(date +%s) - start ))

echo
echo "=============================================================="
printf " Код возврата : %d\n" "${rc}"
printf " Время        : %d мин %d сек\n" $(( elapsed / 60 )) $(( elapsed % 60 ))
printf " Строк в логе : %d\n" "$(wc -l < "${LOG}")"
echo "=============================================================="

if [ ${rc} -ne 0 ]; then
	echo
	echo "--- первая ошибка в логе ---"
	grep -n -m 1 -B 5 -A 20 -E "error:|ERROR:|\*\*\* " "${LOG}" || true
	echo
	echo "--- хвост лога ---"
	tail -25 "${LOG}"
fi

exit ${rc}
