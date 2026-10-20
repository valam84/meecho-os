#!/bin/sh
# Сервер сборки пакетов MEECHO: запускается НА СЕРВЕРЕ, пользователем,
# которому принадлежит каталог сборки (sudo нужен только для docker).
#
#   srv.sh image          - собрать образ meecho-build (port/buildsrv/Dockerfile)
#   srv.sh pkg ИМЯ...     - собрать пакеты рецептами port/pkg/ИМЯ/build.sh
#   srv.sh shell          - шелл в сборочном контейнере
#   srv.sh bulk [список]  - массовая сборка pkgsrc в фоне (bulk.sh)
#   srv.sh bulk-stop      - остановить её после текущего пакета
#   srv.sh serve          - поднять (или перезапустить) раздачу по HTTP
#   srv.sh https          - раздача по HTTPS: https://$PKG_HOST:$HTTPS_PORT/
#   srv.sh status         - что запущено и что лежит в репозитории
#
# Каталог сборки $B (по умолчанию ~/build) устроен как домашний каталог
# minix в WSL и монтируется в контейнер как /home/minix:
#
#   $B/home/minix-src          дерево (кладёт port/buildsrv-sync.sh)
#   $B/home/xtools-aarch64     кросс-тулчейн aarch64-elf64-minix
#   $B/home/dest-evbarm64      DESTDIR - sysroot пакетов
#   $B/home/distfiles          скачанные исходники
#   $B/home/obj-evbarm64/pkg/All   репозиторий: .tgz и pkg_summary
#
# Сервер общий, поэтому сборка ограничена: CPUS ядер (8 из 12), MEM
# памяти, nice 10; контейнеры помечены, чтобы watchtower их не трогал.
# Раздача - отдельный контейнер на порту PORT, только чтение репозитория.
# Порт, опубликованный docker, проходит мимо ufw: открывать его в ufw не
# нужно, закрыть - только убрать контейнер (srv.sh serve с PORT= другим
# или docker rm -f meecho-pkg-http).
set -eu

B=${B:-$HOME/build}
CPUS=${CPUS:-8}
MEM=${MEM:-16g}
JOBS=${JOBS:-$CPUS}
PORT=${PORT:-8095}
PKG_HOST=${PKG_HOST:-pkg.meecho.ru}
HTTPS_PORT=${HTTPS_PORT:-8443}
IMAGE=meecho-build
NOWT="--label com.centurylinklabs.watchtower.enable=false"
DOCKER="sudo -n docker"
SRC=$B/home/minix-src

run() {	# run [ключи docker] -- команда...
	mkdir -p "$B/native-pkg"
	$DOCKER run --rm $NOWT --cpus="$CPUS" --memory="$MEM" \
	    -v "$B/home:/home/minix" -v "$B/native-pkg:/usr/pkg" \
	    -e JOBS="$JOBS" "$@"
}

case ${1:-} in
image)
	$DOCKER build --build-arg UID="$(id -u)" --build-arg GID="$(id -g)" \
	    -t "$IMAGE" "$SRC/port/buildsrv"
	;;
pkg)
	shift
	[ $# -gt 0 ] || { echo "srv.sh pkg ИМЯ..." >&2; exit 2; }
	for p; do
		[ -f "$SRC/port/pkg/$p/build.sh" ] || { echo "нет рецепта $p" >&2; exit 1; }
	done
	mkdir -p "$B/log"
	for p; do
		log=$B/log/$p-$(date +%Y%m%d-%H%M%S).log
		echo "==> $p (журнал $log)"
		run "$IMAGE" nice -n 10 sh "/home/minix/minix-src/port/pkg/$p/build.sh" \
		    > "$log" 2>&1 || { tail -30 "$log"; echo "==> $p: ОТКАЗ" >&2; exit 1; }
		tail -3 "$log"
	done
	;;
shell)
	run -it "$IMAGE" bash
	;;
serve)
	mkdir -p "$B/home/obj-evbarm64/pkg/All"
	$DOCKER rm -f meecho-pkg-http >/dev/null 2>&1 || true
	$DOCKER run -d --name meecho-pkg-http --restart unless-stopped $NOWT \
	    --cpus=1 --memory=256m --read-only \
	    -v "$B/home/obj-evbarm64/pkg:/srv/pkg:ro" -p "$PORT:8000" \
	    "$IMAGE" python3 -m http.server 8000 --directory /srv/pkg
	sleep 1
	curl -sfI "http://127.0.0.1:$PORT/All/" | head -1
	;;
bulk)
	# Массовая сборка pkgsrc (bulk.sh) - отдельным долгим контейнером;
	# ssh можно закрыть. Журнал: docker logs -f meecho-bulk, ~/build/home/bulk/.
	shift
	$DOCKER rm -f meecho-bulk >/dev/null 2>&1 || true
	rm -f "$B/home/bulk/STOP"
	mkdir -p "$B/native-pkg"
	$DOCKER run -d --name meecho-bulk $NOWT --cpus="$CPUS" --memory="$MEM" \
	    -v "$B/home:/home/minix" -v "$B/native-pkg:/usr/pkg" \
	    -e JOBS="$JOBS" "$IMAGE" nice -n 10 sh -c \
	    'sh ~/minix-src/port/buildsrv/pkgsrc-cross.sh && exec sh ~/minix-src/port/buildsrv/bulk.sh "$@"' \
	    bulk "$@"
	;;
bulk-stop)
	# Остановиться после текущего пакета (не убивая его сборку).
	touch "$B/home/bulk/STOP"
	;;
https)
	# Caddy в своём контейнере: сертификат Let's Encrypt (HTTP-01 на 80),
	# раздача на HTTPS_PORT. Сертификаты живут в $B/caddy и переживают
	# пересоздание контейнера.
	mkdir -p "$B/caddy/data" "$B/caddy/config" "$B/home/obj-evbarm64/pkg/All"
	$DOCKER rm -f meecho-pkg-https >/dev/null 2>&1 || true
	$DOCKER run -d --name meecho-pkg-https --restart unless-stopped $NOWT \
	    --cpus=1 --memory=256m -e PKG_HOST="$PKG_HOST" \
	    -v "$SRC/port/buildsrv/Caddyfile:/etc/caddy/Caddyfile:ro" \
	    -v "$B/caddy/data:/data" -v "$B/caddy/config:/config" \
	    -v "$B/home/obj-evbarm64/pkg:/srv/pkg:ro" \
	    -p 80:80 -p "$HTTPS_PORT:8443" caddy:2
	;;
status)
	$DOCKER ps --filter name=meecho --format '{{.Names}} {{.Status}} {{.Ports}}'
	ls -l "$B/home/obj-evbarm64/pkg/All" 2>/dev/null || echo "репозиторий пуст"
	;;
*)
	sed -n '2,20p' "$0" >&2
	exit 2
	;;
esac
