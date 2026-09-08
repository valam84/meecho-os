#!/bin/sh
# Настройка WSL-стороны проброса USB: клиент usbip и права на tty.
#
# Пакет linux-tools-virtual кладёт бинарь в /usr/lib/linux-tools/<версия>/usbip,
# а обёртка /usr/bin/usbip ищет каталог по `uname -r`. У WSL ядро своё
# (6.18.33.2-microsoft-standard-WSL2), такого каталога нет, и обёртка отвечает
# «usbip not found for kernel». Поэтому настоящий бинарь ставится alternative'ом
# в /usr/local/bin, который в PATH стоит раньше /usr/bin.
set -e

TOOLS=$(ls -d /usr/lib/linux-tools/*/ 2>/dev/null | tail -1)
[ -n "$TOOLS" ] || { echo "нет /usr/lib/linux-tools/*: поставь linux-tools-virtual" >&2; exit 1; }
BIN="${TOOLS}usbip"
[ -x "$BIN" ] || { echo "нет $BIN" >&2; exit 1; }

sudo update-alternatives --install /usr/local/bin/usbip usbip "$BIN" 20
echo "usbip -> $(command -v usbip)"
usbip version

# picocom открывает /dev/ttyUSB* от имени пользователя, а не root.
if ! id -nG | tr ' ' '\n' | grep -qx dialout; then
	sudo usermod -aG dialout "$(id -un)"
	echo "пользователь $(id -un) добавлен в dialout — нужен перезапуск WSL (wsl --shutdown)"
else
	echo "dialout: уже есть"
fi
