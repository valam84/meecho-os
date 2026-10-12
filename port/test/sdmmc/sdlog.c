/*
 * sdlog: напечатать хвост журнала драйвера sdmmc, который он держит в памяти.
 *
 *   sdlog [/dev/c1d0]
 *
 * Зачем: консоль платы на 1.5 Мбод теряет знаки посреди строк, и отладочный
 * вывод драйвера оттуда не восстановить. Драйвер пишет те же строки ещё и в
 * кольцо, а SDMMC_IOC_LOG отдаёт его хвост (sdmmc_ioc.h). Собирается
 * кросс-компилятором в программу для MEECHO:
 *
 *   aarch64-elf64-minix-gcc -O -static --sysroot=$HOME/dest-evbarm64 \
 *       -I minix/drivers/storage/mmc -o sdlog port/test/sdmmc/sdlog.c
 */
#include <sys/ioctl.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "sdmmc_ioc.h"

int
main(int argc, char **argv)
{
	const char *dev = (argc > 1) ? argv[1] : "/dev/c1d0";
	static struct sdmmc_logdump d;
	int fd;

	if ((fd = open(dev, O_RDONLY)) < 0) {
		perror(dev);
		return 1;
	}
	if (ioctl(fd, SDMMC_IOC_LOG, &d) < 0) {
		perror("SDMMC_IOC_LOG");
		return 1;
	}
	printf("--- %s: %llu bytes logged, last %u ---\n", dev,
	    (unsigned long long)d.total, (unsigned)d.len);
	fwrite(d.text, 1, d.len, stdout);
	printf("\n");
	close(fd);
	return 0;
}
