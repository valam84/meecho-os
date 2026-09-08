#!/bin/bash
#
# Развёртывание окружения сборки MINIX внутри свежей Ubuntu в WSL.
# Запускается один раз от root:
#
#	wsl -d Ubuntu -u root -- bash $PORT/setup-wsl.sh
#
# Идемпотентен: повторный запуск ничего не ломает.

set -euo pipefail
PORT=$(cd "$(dirname "$0")" && pwd)
SRCDIR=${SRCDIR:-$(cd "$PORT/.." && pwd)}


DEV_USER=minix
SRCDIR=/home/${DEV_USER}/minix-src

echo "=== 1. Пользователь ${DEV_USER} ==="
if id -u "${DEV_USER}" >/dev/null 2>&1; then
	echo "уже существует"
else
	# Пароль не задаётся: вход в WSL идёт без него, а sudo ниже
	# настроен без пароля. Так не приходится нигде хранить секрет.
	adduser --disabled-password --gecos "" "${DEV_USER}"
	usermod -aG sudo "${DEV_USER}"
fi

echo "=== 2. sudo без пароля ==="
echo "${DEV_USER} ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/99-${DEV_USER}
chmod 0440 /etc/sudoers.d/99-${DEV_USER}
visudo -c -f /etc/sudoers.d/99-${DEV_USER}

echo "=== 3. Пользователь по умолчанию ==="
# Чтобы 'wsl -d Ubuntu' сразу попадал в нужного пользователя,
# а не в root. Сборка NetBSD рассчитана на непривилегированный режим.
cat > /etc/wsl.conf <<WSLCONF
[user]
default=${DEV_USER}

[interop]
appendWindowsPath=false
WSLCONF

echo "=== 4. Зависимости сборки ==="
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq

# build.sh собирает собственный кросс-тулчейн, поэтому от хоста нужен
# только компилятор, make и несколько библиотек. Без них дальше нет смысла.
apt-get install -y --no-install-recommends \
	build-essential \
	zlib1g-dev \
	libssl-dev \
	bison \
	flex \
	git \
	curl \
	ca-certificates \
	file \
	bc \
	rsync \
	python3

# Остальное - для сборки образов и отладки в QEMU. Ставим по одному:
# имена пакетов между выпусками Ubuntu меняются, и отсутствие одного
# необязательного инструмента не повод валить всю установку.
for pkg in texinfo dosfstools mtools parted fdisk gdisk \
	qemu-system-arm gdb-multiarch device-tree-compiler; do
	if apt-get install -y --no-install-recommends "${pkg}" >/dev/null 2>&1; then
		echo "  + ${pkg}"
	else
		echo "  ! ${pkg} - не установлен, разберёмся позже"
	fi
done

echo "=== 5. Исходники MINIX на ext4 ==="
# Дерево обязано лежать на ext4: в NetBSD-части есть имена файлов,
# недопустимые в NTFS, и checkout на /mnt/d падает.
if [ -d "${SRCDIR}/.git" ]; then
	echo "уже склонировано: ${SRCDIR}"
else
	sudo -u "${DEV_USER}" git clone \
		https://github.com/Stichting-MINIX-Research-Foundation/minix.git \
		"${SRCDIR}"
fi

echo "=== 6. Проверка ==="
for tool in gcc make qemu-system-arm qemu-system-aarch64 gdb-multiarch; do
	if command -v "${tool}" >/dev/null 2>&1; then
		printf "%-22s %s\n" "${tool}:" "$(${tool} --version 2>&1 | head -1)"
	else
		printf "%-22s ОТСУТСТВУЕТ\n" "${tool}:"
	fi
done
printf "%-22s %s\n" "дерево:" \
	"$(sudo -u "${DEV_USER}" git -C "${SRCDIR}" log -1 --format='%h %ad %s' --date=short)"

echo
echo "=== Готово. ==="
echo "Дерево: ${SRCDIR}"
echo "Дальше: wsl -d Ubuntu -- bash -lc 'cd ~/minix-src && ./build.sh -m evbarm64-el tools'"
