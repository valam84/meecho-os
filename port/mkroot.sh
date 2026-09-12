#!/bin/sh
# Собрать корневую файловую систему MEECHO для eMMC платы.
#
# Содержимое - дерево ramdisk плюс весь userland, который сборка поставила в
# DESTDIR. Ramdisk - это система до тех пор, пока не появится корень, на
# который можно переключиться, и в нём лежит ровно то, без чего этого не
# сделать; на eMMC места гигабайт, и туда идёт всё. Слияние делает
# releasetools/evbarm64_rootproto.py - тем же кодом, что и образ диска для
# QEMU. Отдельно подменяется /etc/rc: у ramdisk это скрипт, который поднимает
# драйвер диска и монтирует диск поверх корня; здесь мы уже на диске.
#
# Почему образ делается на хосте, а не mkfs.mfs прямо на плате. Разметить том
# на месте плата умеет - это проверено, - но заполнить его нечем: содержимое
# ramdisk включает узлы устройств и права, а cp -R по живой системе повторит
# их только если он их понимает. proto же описывает и то и другое буквально, и
# ровно этим путём делается образ диска для QEMU. То есть корень на плате
# получается тем же кодом, что корень в эмуляторе, и различие остаётся ровно
# одно - носитель.
#
#   mkroot.sh [размер в МБ]        -> ~/obj-evbarm64/work/root-emmc.img(.gz)
set -eu
PORT=$(cd "$(dirname "$0")" && pwd)
SRCDIR=${SRCDIR:-$(cd "$PORT/.." && pwd)}


SRC=${SRC:-$SRCDIR}
OBJ=${OBJ:-$HOME/obj-evbarm64}
CROSS_TOOLS=${CROSS_TOOLS:-$HOME/tools-evbarm64/bin}
DEST=${DEST:-$HOME/dest-evbarm64}
WORK=${WORK:-${OBJ}/work}
MB=${1:-1024}
BS=4096

RAMDISK_OBJ=${OBJ}/minix/drivers/storage/ramdisk
MKFSMFS=${MKFSMFS:-${CROSS_TOOLS}/nbmkfs.mfs}
IMG=${WORK}/root-emmc.img

[ -f "${RAMDISK_OBJ}/proto.gen" ] || {
	echo "$0: нет ${RAMDISK_OBJ}/proto.gen - сначала собрать ramdisk" >&2
	exit 1
}
mkdir -p "${WORK}"

cat > "${WORK}/rc.emmc" <<'END_RC'
#!/bin/sh
# /etc/rc корня на eMMC. Ramdisk уже поднял драйвер sdmmc, проверил этот том
# и смонтировал его поверх корня; init запускает этот скрипт, а когда тот
# вернётся - сессии из /etc/ttys.
PATH=/sbin:/usr/sbin:/bin:/usr/bin
export PATH

# Маска прав по умолчанию: её не ставит никто (см. комментарий в
# etc/profile), и без этой строки службы, запущенные отсюда, - в том
# числе sshd - создают файлы доступными на запись всем.
umask 022

echo "Root is on `sysenv rootdevname`, driver `sysenv blkdrv`."

# Источник случайности: нужен всему, что дальше, начиная с секрета
# начальных номеров TCP.
minix-service up /service/random -dev /dev/random ||
    echo "WARNING: no random device"

# Локальные сокеты (AF_UNIX). Их даёт отдельная служба, и без неё
# socketpair(2) отказывает с ENOENT - VFS просто некому передать домен
# LOCAL. Нашлось на sshd: он заводит socketpair, чтобы разговаривать со
# своим sshd-session, и без него соединение рвётся сразу после установки
# TCP - "reexec socketpair: No such file or directory" в его отладке и
# "kex_exchange_identification: Connection reset" у клиента.
minix-service up /service/uds ||
    echo "WARNING: no local (AF_UNIX) sockets"


# Сеть. Этот скрипт живёт только на плате, поэтому по умолчанию здесь
# драйвер платы, а не эмулятора; аргумент загрузки netdrv= его перекрывает.
if sysenv netdrv >/dev/null
then	netdrv="`sysenv netdrv`"
else	netdrv=dwmac
fi

if [ -x "/service/$netdrv" ]
then
	echo "Starting the network driver $netdrv"
	# The driver's log level comes from the boot arguments, so a run that
	# has to be looked at closely does not need a new root image.
	if sysenv netlog >/dev/null
	then	netlog="`sysenv netlog`"
	else	netlog=3
	fi
	minix-service up "/service/$netdrv" -label "${netdrv}_0" \
	    -args "instance=0 log=$netlog" ||
	    echo "WARNING: no network driver"
	minix-service up /service/lwip -dev /dev/bpf ||
	    echo "WARNING: no network stack"

	# Адрес спрашивается у сети. Статического запасного здесь нет
	# намеренно: на плате сеть настоящая, и адрес, придуманный на случай
	# молчания сервера, попал бы в чужую подсеть и выглядел бы как
	# рабочая настройка. Молчание должно выглядеть молчанием.
	netif=
	for i in `ifconfig -l 2>/dev/null`
	do
		if [ "$i" != lo0 ]
		then	netif="$i"
			break
		fi
	done
	ifconfig lo0 inet 127.0.0.1 up 2>/dev/null
	if [ -n "$netif" ] && [ -x /sbin/dhcpcd ]
	then
		ifconfig "$netif" up
		echo "Asking for an address on $netif"
		# Клиент говорит всё, что делает, в файл, а не на консоль: там
		# это утонуло бы в отладке драйвера, а тут остаётся на потом.
		# До ухода в фон - то есть ровно та часть, где он получает
		# аренду и ставит адрес и маршруты; дальше он пишет в syslog,
		# которого здесь нет. Один прогон уже показал, зачем это:
		# маршрут по умолчанию при загрузке пропадал молча.
		if dhcpcd -d -t 20 "$netif" > /var/log/dhcpcd.log 2>&1
		then	echo "Leased on $netif:"
			ifconfig "$netif" | grep 'inet '
		else	echo "WARNING: no lease on $netif (see /var/log/dhcpcd.log)"
		fi
	else
		ifconfig -a 2>/dev/null || echo "(no ifconfig on this root)"
	fi
fi

# Псевдотерминалы. Без них ssh пускает, но интерактивной сессии не даёт:
# шеллу нужен управляющий терминал, а openpty(3) берёт его у этого
# драйвера. Пара /dev/ptypN + /dev/ttypN, майор 9; ptyfs и /dev/pts тут
# не нужны - openpty(3) на MINIX сначала пробует путь Unix98, а он
# требует ptyfs, и молча откатывается на пары, которые работают у root.
# sshd отводит терминал до того, как сбросит права, то есть как root.
if [ -x /service/pty ]
then
	minix-service up /service/pty -dev /dev/ptyp0 ||
	    echo "WARNING: no pseudo terminals"
fi

# ssh. Ключ хоста делается ЗДЕСЬ, на первой загрузке, а не кладётся в образ:
# ключ, лежащий в образе, одинаков у всех, кто этот образ развернул, и
# перестаёт быть ключом. Отсюда же требование к предыдущему шагу: ssh-keygen
# берёт случайность из arc4random(3), тот - из /dev/urandom, а тот посеян
# только если у драйвера random есть источник. На этой плате источник
# аппаратный (см. minix/drivers/system/random/trng.c); если его нет, ключ не
# сделается, и это будет видно в журнале, а не тихо.
# Посеян ли пул - спросить ПРЯМО, а не надеяться. arc4random(3), из
# которого ssh-keygen берёт случайность, при пустом /dev/random не
# отказывает, а переходит на слабую замену (время, pid, адрес стека) и
# молча делает ключ. Ключ получится, и он будет плохой. Пусть это
# видно в журнале загрузки.
if ! dd if=/dev/random bs=1 count=1 >/dev/null 2>&1
then
	echo "WARNING: /dev/random is not seeded; keys made now are weak"
fi

if [ -x /usr/bin/ssh-keygen ] && [ ! -f /etc/ssh/ssh_host_ed25519_key ]
then
	echo "Generating an SSH host key"
	/usr/bin/ssh-keygen -q -t ed25519 -N '' 	    -f /etc/ssh/ssh_host_ed25519_key ||
	    echo "WARNING: could not generate a host key"
fi

if [ -x /usr/sbin/sshd ] && [ -f /etc/ssh/ssh_host_ed25519_key ]
then
	echo "Starting sshd"
	/usr/sbin/sshd || echo "WARNING: sshd did not start"
fi

exit 0
END_RC

# Какие каталоги DESTDIR попадают в корень. Без SUBDIRS - все, что
# rootproto.py берёт по умолчанию, то есть весь userland; со списком - только
# он. Второе нужно, когда корень едет на плату по сети ради одной проверки:
# гигабайт userland в образе стоит десятков мегабайт в gzip и минут в scp, а
# служба, которую надо посмотреть, весит сто килобайт.
python3 "${SRC}/releasetools/evbarm64_rootproto.py" \
	"${RAMDISK_OBJ}/proto.gen" "${RAMDISK_OBJ}" "${DEST}" ${SUBDIRS:-} \
	> "${WORK}/proto.full"
sed "s|^\([ 	]*rc ---755 0 0 \).*|\1${WORK}/rc.emmc|" \
	"${WORK}/proto.full" > "${WORK}/proto.emmc"

# Открытый ключ, которому разрешён вход как root. В дереве ОС ему не место
# - это ключ разработчика, а не часть системы, - поэтому он подставляется в
# proto здесь. Без файла sshd просто не найдёт ключа и никого не пустит:
# sshd_config называет два места, и второе (~/.ssh/authorized_keys)
# остаётся за пользователем.
AUTHKEYS=${AUTHKEYS:-$PORT/authorized_keys}
if [ -f "${AUTHKEYS}" ]
then
	cp "${AUTHKEYS}" "${WORK}/authorized_keys"
	# Вставить строку рядом с sshd_config, в том же каталоге proto и с тем
	# же отступом. awk, а не sed: перевод строки в замене sed пришлось бы
	# экранировать, а он тут и так экранирован дважды.
	awk -v key="${WORK}/authorized_keys" '
		{ print }
		/sshd_config ---644 0 0 / {
			pre = $0
			sub(/sshd_config.*/, "", pre)
			print pre "authorized_keys ---644 0 0 " key
		}' "${WORK}/proto.emmc" > "${WORK}/proto.emmc.new"
	mv "${WORK}/proto.emmc.new" "${WORK}/proto.emmc"
	echo "authorized_keys: ${AUTHKEYS}"
else
	echo "$0: нет ${AUTHKEYS} - вход по ключу не настроен" >&2
fi

# Откуда плата берёт пакеты: /usr/pkg/etc/pkg_install.conf с PKG_PATH на
# репозиторий хаба (pkg-publish.sh). Это настройка площадки, как и ключ
# выше, поэтому она подставляется здесь, а не лежит в дереве. Каталог
# usr/pkg/etc в proto уже есть - из скелета hier(7); файл встаёт в него
# тем же приёмом, что authorized_keys: awk находит "etc" сразу под "pkg".
PKG_REPO_URL=${PKG_REPO_URL:-http://192.168.33.2:8080/All}
printf 'PKG_PATH=%s\n' "${PKG_REPO_URL}" > "${WORK}/pkg_install.conf"
awk -v conf="${WORK}/pkg_install.conf" '
	{ print }
	/^\t\tpkg d--/ { inpkg = 1; next }
	inpkg && /^\t\t\tetc d--/ {
		print "\t\t\t\tpkg_install.conf ---644 0 0 " conf
		inpkg = 0
	}
	/^\t\t?[^\t]/ { inpkg = 0 }' \
	"${WORK}/proto.emmc" > "${WORK}/proto.emmc.new"
if grep -q pkg_install.conf "${WORK}/proto.emmc.new"
then
	mv "${WORK}/proto.emmc.new" "${WORK}/proto.emmc"
	echo "pkg_install.conf: PKG_PATH=${PKG_REPO_URL}"
else
	echo "$0: в proto нет usr/pkg/etc - PKG_PATH не задан" >&2
	rm -f "${WORK}/proto.emmc.new"
fi

rm -f "${IMG}" "${IMG}.gz"
dd if=/dev/zero of="${IMG}" bs=1M count=0 seek=${MB} 2>/dev/null

# proto называет файлы абсолютными путями, каталог запуска не важен.
${MKFSMFS} -4 -B ${BS} \
	-b $((MB * 1024 * 1024 / BS)) "${IMG}" "${WORK}/proto.emmc"

gzip -9 -c "${IMG}" > "${IMG}.gz"
ls -l "${IMG}" "${IMG}.gz"
echo "готово: ${IMG}.gz"
