# MEECHO: одноразовая загрузка с вендорской карты CB2.
#
# Зачем это есть. Круг разработки на плате стоил «вынуть карту, скопировать
# два файла, вставить, дёрнуть питание», и пока это так, любая работа с
# железом дороже, чем должна быть. Веха 8.1 решает это загрузкой по TFTP; тут
# то же самое достигается иначе и без сервера: у платы есть ssh (заводской
# Armbian на карте), значит файлы можно положить прямо в /boot по сети, а
# выбор «эту загрузку — MEECHO» сделать файлом-флагом.
#
# Почему именно fixup.scr, а не свой boot.scr. Вендорский скрипт сам делает
#
#	if test -e ${devtype} ${devnum}:${distro_bootpart} ${prefix}fixup.scr; then
#		load ... ${load_addr} ${prefix}fixup.scr
#		source ${load_addr}
#	fi
#
# прямо перед своим booti. То есть точка входа для нас уже предусмотрена, и
# вендорский boot.scr остаётся байт в байт таким, каким его положил Armbian:
# удаление одного файла /boot/fixup.scr возвращает плату к заводской загрузке
# полностью. Своя копия boot.scr этого свойства не имеет — её пришлось бы
# сопровождать при каждом обновлении Armbian.
#
# Одноразовость. Флаг снимается до booti, а не после, потому что после уже
# некому: MEECHO не умеет писать в FAT (пока нет драйвера MMC — ради него всё
# это и делается). Снятый флаг означает, что следующая перезагрузка — из
# MEECHO командой reboot или по питанию — приведёт обратно в Linux, то есть к
# ssh. Если снять флаг не удалось, MEECHO не грузится вовсе: лучше остаться в
# Linux, чем уйти в дверь, которая закрывается за спиной.
#
# Адреса — те же, что в cb2-boot.cmd (см. обоснование там). Вендорский скрипт
# к этому месту уже разложил своё ядро по 0x02080000 и initrd по 0x0a200000,
# наши адреса с ними не пересекаются.

if test -e ${devtype} ${devnum}:${distro_bootpart} ${prefix}meecho.go; then
	echo "MEECHO: one-shot flag found, clearing it"
	fatrm ${devtype} ${devnum}:${distro_bootpart} ${prefix}meecho.go

	if test -e ${devtype} ${devnum}:${distro_bootpart} ${prefix}meecho.go; then
		echo "MEECHO: the flag is still there - staying with Linux"
	else
		# Скорость консоли переключается файлом, как и на карте MEECHO.
		#
		# Плата печатает на 1500000 - это 150 КБ/с, и CH340, приезжающий
		# сюда по сети через USB Network Gate, столько не вывозит: куски
		# теряются посреди строк, причём одинаково у MEECHO и у Armbian,
		# так что дело в мосте, а не в системе. На 115200 поток в
		# тринадцать раз тоньше и журнал читается целиком.
		#
		# Хватает одного setenv: ни ядро, ни tty делитель не пишут -
		# и dw8250.c, и ns8250.c намеренно наследуют то, что оставила
		# прошивка (см. комментарии в них). Поэтому скорость меняется
		# здесь, в одном месте, а не в трёх.
		#
		# U-Boot после смены печатает "press ENTER" и ждёт возврата
		# каретки на новой скорости: терминал должен слать его сам.
		# Флаг загрузки к этому моменту уже снят, так что если ENTER не
		# придёт и плата встанет в U-Boot, питание вернёт её в Linux.
		if test -e ${devtype} ${devnum}:${distro_bootpart} ${prefix}meecho/slow_console; then
			echo "MEECHO: switching console to 115200"
			setenv baudrate 115200
		fi

		# Корень: по умолчанию ramdisk в памяти, то есть ровно та
		# загрузка, которая на этой плате уже работала. Файл
		# meecho/root_emmc переключает на корень с eMMC - отдельным
		# переключателем, потому что первый прогон нового драйвера
		# не должен стоять на пути к приглашению login.
		if test -e ${devtype} ${devnum}:${distro_bootpart} ${prefix}meecho/root_emmc; then
			setenv bootargs "rootdevname=c0d0 blkdrv=sdmmc console=tty00"
			echo "MEECHO: root on eMMC (meecho/root_emmc present)"
		else
			setenv bootargs "bootramdisk=1 console=tty00"
		fi

		# Тот же переключатель файлом, что и на карте MEECHO. SMP на
		# плате проверен (2026-09-08, веха 8.0.3), поэтому файла тут
		# по умолчанию нет; он нужен, чтобы получить ту же машину на
		# одном ядре — единственный честный эталон для замера.
		if test -e ${devtype} ${devnum}:${distro_bootpart} ${prefix}meecho/no_smp; then
			setenv bootargs "${bootargs} no_smp=1"
			echo "MEECHO: single CPU (meecho/no_smp present)"
		fi

		# Подробная загрузка: ядро печатает, какой образ оно
		# инициализирует и на каком шаге находится. Нужно тогда,
		# когда система замолчала и неизвестно, докуда дошла.
		if test -e ${devtype} ${devnum}:${distro_bootpart} ${prefix}meecho/verbose; then
			setenv bootargs "${bootargs} verbose=3"
			echo "MEECHO: verbose boot (meecho/verbose present)"
		fi

		# Сторож загрузки: ядро само сбросит машину через это число
		# секунд. Плата стоит не здесь, и загрузка, не дошедшая до
		# приглашения, иначе оставляет её мёртвой до человека.
		#
		# Три ступени, а не две. Пятнадцать минут - под прогон по
		# расписанию, но мало под работу руками по ssh, а снятый
		# сторож (no_bootwd) означает, что зависшую плату вернёт
		# только человек у выключателя. Поэтому между ними есть час:
		# сессии хватает, спасение остаётся. no_bootwd проверяется
		# первым - он сильнее.
		if test -e ${devtype} ${devnum}:${distro_bootpart} ${prefix}meecho/no_bootwd; then
			echo "MEECHO: boot watchdog off (meecho/no_bootwd present)"
		elif test -e ${devtype} ${devnum}:${distro_bootpart} ${prefix}meecho/bootwd_long; then
			setenv bootargs "${bootargs} bootwd=3600"
			echo "MEECHO: boot watchdog 3600s (meecho/bootwd_long present)"
		else
			setenv bootargs "${bootargs} bootwd=900"
		fi

		# Разведка: какие команды у этой сборки U-Boot вообще есть.
		# Печатает весь список и снимается удалением файла.
		if test -e ${devtype} ${devnum}:${distro_bootpart} ${prefix}meecho/netprobe; then
			echo "MEECHO: netprobe begin"
			help
			echo "MEECHO: netprobe end"
		fi

		# Ядро и архив: сначала по сети, потом с карты.
		#
		# Сервер задан здесь, а не приходит от DHCP: штатный DHCP
		# сети - это домашний маршрутизатор, next-server он не
		# раздаёт, и serverip после dhcp остаётся пустым. Окружение
		# U-Boot этой сборки не сохраняется ("Environment from
		# nowhere"), так что записать адрес один раз некуда - только
		# в скрипт.
		#
		# netretry no: без него неудачная сеть стоит десятков секунд
		# повторов на каждой загрузке, а откат на карту нужен быстрым.
		setenv meecho_src card
		setenv autoload no
		setenv netretry no
		setenv tftpblocksize 1468
		# Попытка по сети - по флагу, а не по умолчанию: U-Boot этой
		# сборки собран без CONFIG_NET вовсе (проверено списком
		# команд: ни tftpboot, ни ping, ни bootp), и без флага
		# каждая загрузка печатала бы "Unknown command 'dhcp'".
		# Код оставлен готовым к загрузчику, у которого сеть есть.
		if test -e ${devtype} ${devnum}:${distro_bootpart} ${prefix}meecho/net; then
			if dhcp; then
				setenv serverip 192.168.33.2
				echo "MEECHO: trying ${serverip} over tftp"
				if tftpboot 0x40200000 meecho/kernel.bin; then
					if tftpboot 0x48000000 meecho/boot.mba; then
						setenv meecho_mba_size ${filesize}
						setenv meecho_src net
					fi
				fi
			fi
		fi

		# tftpboot умеет вернуть успех и оставить filesize от
		# прошлой загрузки, поэтому источник отмечается своей
		# переменной, а не выводится из filesize.
		if test "${meecho_src}" = "net"; then
			echo "MEECHO: loaded over tftp"
		else
			echo "MEECHO: loading from ${devtype} ${devnum}:${distro_bootpart}"
			load ${devtype} ${devnum}:${distro_bootpart} 0x40200000 ${prefix}meecho/kernel.bin
			load ${devtype} ${devnum}:${distro_bootpart} 0x48000000 ${prefix}meecho/boot.mba
			setenv meecho_mba_size ${filesize}
		fi

		# Дерево по сети не возят: оно не меняется и читается с карты
		# за двадцать пять миллисекунд.
		load ${devtype} ${devnum}:${distro_bootpart} 0x4a000000 ${prefix}dtb/rockchip/rk3566-bigtreetech-cb2.dtb

		echo "MEECHO: booting, archive ${meecho_mba_size} bytes"
		booti 0x40200000 0x48000000:${meecho_mba_size} 0x4a000000

		# Сюда управление попадает, только если booti отказал; тогда
		# вендорский скрипт продолжится и загрузит Linux.
		echo "MEECHO: booti returned - falling through to Linux"
	fi
fi
