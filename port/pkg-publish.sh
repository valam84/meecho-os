#!/bin/sh
# Опубликовать репозиторий пакетов на хабе и убедиться, что он раздаётся
# по HTTP.
#
#   pkg-publish.sh            - положить ~/obj-evbarm64/pkg/All на хаб
#
# Хаб - та Linux-машина в одной сети с платой, к которой воткнут её
# USB-TTL; она же держит tftp (board-tftp.sh). Пакеты ложатся в
# /srv/pkg/All, раздаёт их python3 -m http.server на порту 8080 - без
# отдельного веб-сервера, которого на хабе нет; поднимается здесь же, если
# ещё не слушает, и переживает выход из ssh (setsid + nohup).
#
# На плате после этого:
#   pkg_add lua              (PKG_PATH стоит в /usr/pkg/etc/pkg_install.conf,
#                             его туда кладёт mkroot.sh)
#   PKG_PATH=http://192.168.33.2:8080/All pkg_add lua   (руками)
#
# Запускается из Git Bash: пути WSL берутся через /wsl.localhost, ssh -
# виндовый, с ключом из профиля, как у board-tftp.sh.
set -eu
. "$(dirname "$0")/board.conf" 2>/dev/null || true
HUB=${HUB:-root@94.45.222.236}
HUB_PORT=${HUB_PORT:-17466}
KEY=${KEY:-$USERPROFILE/.ssh/id_ed25519_crm76}
WSL_DISTRO=${WSL_DISTRO:-Ubuntu}
WSL_USER=${WSL_USER:-minix}
REPO=${REPO:-//wsl.localhost/$WSL_DISTRO/home/$WSL_USER/obj-evbarm64/pkg/All}
HUBDIR=${HUBDIR:-/srv/pkg}
HUBPORT=${HUBPORT:-8080}
SSH="ssh -o BatchMode=yes -o ConnectTimeout=15 -i $KEY -p $HUB_PORT"
SCP="scp -o BatchMode=yes -o ConnectTimeout=15 -i $KEY -P $HUB_PORT"

ls "$REPO"/*.tgz >/dev/null 2>&1 || { echo "в $REPO нет пакетов" >&2; exit 2; }

$SSH "$HUB" "mkdir -p $HUBDIR/All"
# MSYS_NO_PATHCONV: иначе Git Bash перепишет "$HUB:/srv/..." в виндовый путь.
MSYS_NO_PATHCONV=1 $SCP "$REPO"/*.tgz "$HUB:$HUBDIR/All/"
MSYS_NO_PATHCONV=1 $SSH "$HUB" "cd $HUBDIR && chmod 644 All/*.tgz && \
	if ! ss -ltn 2>/dev/null | grep -q ':$HUBPORT '; then \
		setsid nohup python3 -m http.server $HUBPORT --directory $HUBDIR \
			>/var/log/pkg-http.log 2>&1 < /dev/null & \
		sleep 1; \
	fi; \
	ls -l All; \
	curl -sfI http://127.0.0.1:$HUBPORT/All/ | head -1"
echo "готово: http://192.168.33.2:$HUBPORT/All/"
