#!/bin/sh
# Положить на сервер сборки пакетов то, против чего собираются пакеты.
# Запускается из Git Bash рабочей машины (как pkg-publish.sh): WSL делает
# архивы, виндовый ssh/scp их везёт.
#
#   buildsrv-sync.sh tree     - дерево на HEAD ветки (git archive, без
#                               незакоммиченного: на сервер едет только то,
#                               что записано в истории)
#   buildsrv-sync.sh sysroot  - DESTDIR (~/dest-evbarm64) целиком
#   buildsrv-sync.sh xtools   - готовый кросс-тулчейн из WSL
#   buildsrv-sync.sh cxx      - он же с C++ (второй проход build-xtools.sh)
#   buildsrv-sync.sh all      - всё три
#
# sysroot - то, против чего линкуется каждый пакет: после правки libc или
# заголовков в DESTDIR его надо привезти заново, иначе пакеты соберутся
# против старой libc. Тулчейн везётся готовым, пока он один и тот же; его
# же можно собрать на сервере (srv.sh shell; bash minix-src/port/build-xtools.sh all).
#
# Дальше на сервере: ~/build/home/minix-src/port/buildsrv/srv.sh (см. там).
set -eu
SRV=${SRV:-meecho@116.202.192.98}
KEY=${KEY:-$USERPROFILE/.ssh/id_ed25519_meecho_build}
B=${B:-build}				# относительно домашнего каталога на сервере
WSL_DISTRO=${WSL_DISTRO:-Ubuntu}
WSL_USER=${WSL_USER:-minix}
XFER=${XFER:-/d/minix/.buildsrv-xfer}	# промежуточный каталог на Windows
XFER_WSL=/mnt/d/minix/.buildsrv-xfer
SSH="/c/Windows/System32/OpenSSH/ssh.exe -o BatchMode=yes -o ConnectTimeout=15 -o ServerAliveInterval=15 -i $KEY"
SCP="/c/Windows/System32/OpenSSH/scp.exe -o BatchMode=yes -o ConnectTimeout=15 -o ServerAliveInterval=15 -i $KEY"
export MSYS_NO_PATHCONV=1

wsl_run() { wsl.exe -d "$WSL_DISTRO" -u "$WSL_USER" -- bash -c "$1"; }

ship() {	# ship имя-архива каталог-на-сервере
	$SCP "$(cygpath -m "$XFER/$1")" "$SRV:$B/$1"
	$SSH "$SRV" "set -e; mkdir -p $B/home; rm -rf $B/home/$2.new; mkdir $B/home/$2.new; \
		tar -C $B/home/$2.new -xzf $B/$1; rm -f $B/$1; \
		rm -rf $B/home/$2.old; [ -d $B/home/$2 ] && mv $B/home/$2 $B/home/$2.old; \
		mv $B/home/$2.new $B/home/$2; rm -rf $B/home/$2.old"
	rm -f "$XFER/$1"
}

tree() {
	wsl_run "set -e; mkdir -p $XFER_WSL; cd /home/minix/minix-src; \
		git rev-parse HEAD > $XFER_WSL/commit; \
		git archive --format=tar HEAD | gzip -1 > $XFER_WSL/tree.tar.gz"
	ship tree.tar.gz minix-src
	$SCP "$(cygpath -m "$XFER/commit")" "$SRV:$B/home/minix-src/.commit"
	echo "дерево: $(cat "$XFER/commit")"
	rm -f "$XFER/commit"
}

dir() {		# dir имя-каталога-в-домашнем-WSL
	wsl_run "set -e; mkdir -p $XFER_WSL; cd /home/minix/$1; \
		tar -cf - . | gzip -1 > $XFER_WSL/$1.tar.gz"
	ship "$1.tar.gz" "$1"
	echo "$1: привезён"
}

mkdir -p "$XFER"
case ${1:-} in
tree)    tree ;;
sysroot) dir dest-evbarm64 ;;
xtools)  dir xtools-aarch64 ;;
cxx)     dir xtools-aarch64-cxx ;;
all)     tree; dir dest-evbarm64; dir xtools-aarch64 ;;
*)       sed -n '2,15p' "$0" >&2; exit 2 ;;
esac
rmdir "$XFER" 2>/dev/null || true
