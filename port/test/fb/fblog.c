/*
 * fblog: напечатать хвост журнала драйвера кадрового буфера (fb), который
 * он держит в памяти, и его текущее состояние.
 *
 *   fblog [/dev/fb0]
 *
 * Зачем: консоль платы на 1.5 Мбод теряет знаки посреди строк, а журнал
 * fb - это в основном сверка регистров с эталоном, которую по обрывкам не
 * восстановить. Драйвер пишет те же строки ещё и в кольцо, а FBIOC_LOG
 * отдаёт его хвост (arch/aarch64/fb_ioc.h) и перед этим заново спрашивает
 * железо: вставлен ли монитор, захвачен ли PLL PHY, бежит ли развёртка.
 * Так что «воткнул монитор - спросил» работает без перезагрузки.
 * Собирается кросс-компилятором в программу для MEECHO:
 *
 *   aarch64-elf64-minix-gcc -O -static --sysroot=$HOME/dest-evbarm64 \
 *       -I minix/drivers/video/fb/arch/aarch64 -o fblog port/test/fb/fblog.c
 */
#include <sys/ioctl.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "fb_ioc.h"

int
main(int argc, char **argv)
{
	const char *dev = (argc > 1) ? argv[1] : "/dev/fb0";
	static struct fb_logdump d;
	int fd;

	if ((fd = open(dev, O_RDONLY)) < 0) {
		perror(dev);
		return 1;
	}
	if (ioctl(fd, FBIOC_LOG, &d) < 0) {
		perror("FBIOC_LOG");
		return 1;
	}
	printf("--- %s: %llu bytes logged, last %u ---\n", dev,
	    (unsigned long long)d.total, (unsigned)d.len);
	fwrite(d.text, 1, d.len, stdout);
	printf("\n");
	close(fd);
	return 0;
}
