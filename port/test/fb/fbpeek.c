/*
 * fbpeek: прочитать кусок кадрового буфера read()'ом и напечатать его
 * знаками - '#' там, где пиксель не чёрный (любой канал больше 0x40),
 * '.' где чёрный: консоль рисует на чёрном фоне, и голубые сообщения ядра
 * (0x00aaaa) так же видны, как серый текст.
 *
 *   fbpeek [строка [первый-столбец [столбцов]]]
 *
 * Строка и столбцы - в клетках консоли 8x16, по умолчанию строка 0, столбцы
 * 0..79. Зачем: без монитора это единственный способ увидеть, что консоль
 * на экране действительно нарисовала буквы, а не только приняла текст, -
 * память кадра та самая, которую развёртка VOP2 читает (что она её читает,
 * драйвер доказывает сам: счётчик строк, нет недогрузки). Открытие
 * /dev/fb0 ставит консоль на паузу, закрытие перерисовывает её целиком,
 * так что читается ровно то, что было на экране.
 *
 *   aarch64-elf64-minix-gcc -O -static --sysroot=$HOME/dest-evbarm64 \
 *       -o fbpeek port/test/fb/fbpeek.c
 */
#include <sys/ioctl.h>
#include <sys/types.h>
#include <minix/fb.h>
#include <sys/ioc_fb.h>

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
	struct fb_var_screeninfo v;
	struct fb_fix_screeninfo f;
	unsigned row = 0, col = 0, ncols = 80, y, x;
	uint32_t *line, p, r, g, b;
	int fd;

	if (argc > 1)
		row = (unsigned)atoi(argv[1]);
	if (argc > 2)
		col = (unsigned)atoi(argv[2]);
	if (argc > 3)
		ncols = (unsigned)atoi(argv[3]);

	if ((fd = open("/dev/fb0", O_RDONLY)) < 0) {
		perror("/dev/fb0");
		return 1;
	}
	if (ioctl(fd, FBIOGET_VSCREENINFO, &v) < 0 ||
	    ioctl(fd, FBIOGET_FSCREENINFO, &f) < 0) {
		perror("FBIOGET_*SCREENINFO");
		return 1;
	}
	if (v.bits_per_pixel != 32 || (col + ncols) * 8 > v.xres ||
	    (row + 1) * 16 > v.yres) {
		fprintf(stderr, "fbpeek: outside the %ux%u screen\n", v.xres,
		    v.yres);
		return 1;
	}
	if ((line = malloc(ncols * 8 * 4)) == NULL)
		return 1;
	for (y = row * 16; y < row * 16 + 16; y++) {
		if (pread(fd, line, ncols * 8 * 4,
		    (off_t)y * f.line_length + col * 8 * 4) !=
		    (ssize_t)(ncols * 8 * 4)) {
			perror("read");
			return 1;
		}
		for (x = 0; x < ncols * 8; x++) {
			p = line[x];
			r = (p >> v.red.offset) & 0xff;
			g = (p >> v.green.offset) & 0xff;
			b = (p >> v.blue.offset) & 0xff;
			putchar((r | g | b) > 0x40 ? '#' : '.');
		}
		putchar('\n');
	}
	close(fd);
	return 0;
}
