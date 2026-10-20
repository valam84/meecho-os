#!/bin/bash
# MEECHO в QEMU ставит пакеты из репозитория и запускает их - проверка
# того, что пакет, собранный на сервере сборки (port/buildsrv), живёт на
# самой системе, а не только собирается.
#
#   qemu-pkgadd.sh ПАКЕТ 'КОМАНДА' [ПАКЕТ 'КОМАНДА']...
#   qemu-pkgadd.sh figlet '/usr/pkg/bin/figlet MEECHO'
#
# Репозиторий - REPO_URL (по умолчанию https://pkg.meecho.ru:8443/All):
# путь целиком настоящий - DHCP, DNS, TLS с проверкой сертификата
# (время гостю даёт PL031 QEMU), libfetch, pkg_add. Свой WORK_DIR со
# свежим диском: общий ~/obj-evbarm64/work трогать нельзя - им живут
# другие стенды.
#
# Итог - журнал консоли в $WORK_DIR.log и строки PKGTEST в конце: что
# поставилось и что напечатала команда. Код 0 - если каждая команда
# дошла до метки после себя.
set -u
REPO_URL=${REPO_URL:-https://pkg.meecho.ru:8443/All}
export WORK_DIR=${WORK_DIR:-/home/minix/obj-evbarm64/work-pkgtest}
export DISK_FRESH=1 DISK_MB=${DISK_MB:-512} QEMU_TIMEOUT=${QEMU_TIMEOUT:-900}
LOG=$WORK_DIR.log
[ $# -ge 2 ] && [ $(($# % 2)) -eq 0 ] || { sed -n '6,8p' "$0" >&2; exit 2; }

script() {
	sleep "${BOOT_WAIT:-90}"
	echo root; sleep 5
	echo 'echo PKGTEST date: $(date)'; sleep 3
	while [ $# -gt 0 ]; do
		echo "PKG_PATH=$REPO_URL pkg_add -v $1; printf 'PKGTEST add-%s rc=%s\\n' $1 \$?"
		sleep "${ADD_WAIT:-120}"
		echo "$2 > /tmp/out 2>&1; r=\$?; sed 's/^/PKGTEST out: /' /tmp/out; printf 'PKGTEST run-%s rc=%s\\n' $1 \$r"
		sleep 15
		shift 2
	done
	echo /sbin/poweroff; sleep 15
}

script "$@" | bash /home/minix/bin/ramimage.sh -d > "$LOG" 2>&1
tr -d '\r' < "$LOG" | grep -a '^PKGTEST' | grep -av 'printf'
fail=0
while [ $# -gt 0 ]; do
	tr -d '\r' < "$LOG" | grep -aq "^PKGTEST run-$1 rc=0" || { echo "FAIL: $1"; fail=1; }
	shift 2
done
exit $fail
