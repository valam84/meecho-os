/*
 * fbtest: нарисовать картинку в /dev/fb0 обычной программой - проверка
 * устройства кадрового буфера, а не драйвера изнутри.
 *
 *   fbtest [-s секунды] [узор]
 *
 * Узоры: rings (по умолчанию) - концентрические цветные кольца по всему
 * экрану с крестом через центр; checker - шахматная доска 32x32. Программа
 * спрашивает геометрию ioctl'ами FBIOGET_VSCREENINFO / FBIOGET_FSCREENINFO,
 * собирает строку и пишет её write()'ом в нужное место (lseek), то есть
 * идёт ровно тем путём, каким пошла бы любая графическая программа.
 * Пока /dev/fb0 открыт, консоль на экране стоит; на закрытии (через -s
 * секунд, по умолчанию 20) она рисуется заново.
 *
 *   aarch64-elf64-minix-gcc -O -static --sysroot=$HOME/dest-evbarm64 \
 *       -o fbtest port/test/fb/fbtest.c
 */
#include <sys/ioctl.h>
#include <sys/types.h>
#include <minix/fb.h>
#include <sys/ioc_fb.h>

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static uint32_t
rgb(const struct fb_var_screeninfo *v, unsigned r, unsigned g, unsigned b)
{
	return (r & 0xff) << v->red.offset | (g & 0xff) << v->green.offset |
	    (b & 0xff) << v->blue.offset;
}

int
main(int argc, char **argv)
{
	struct fb_var_screeninfo v;
	struct fb_fix_screeninfo f;
	const char *pattern = "rings";
	unsigned seconds = 20, x, y;
	uint32_t *row;
	int fd, c, dx, dy, d;

	while ((c = getopt(argc, argv, "s:")) != -1)
		if (c == 's')
			seconds = (unsigned)atoi(optarg);
	if (optind < argc)
		pattern = argv[optind];

	if ((fd = open("/dev/fb0", O_RDWR)) < 0) {
		perror("/dev/fb0");
		return 1;
	}
	if (ioctl(fd, FBIOGET_VSCREENINFO, &v) < 0 ||
	    ioctl(fd, FBIOGET_FSCREENINFO, &f) < 0) {
		perror("FBIOGET_*SCREENINFO");
		return 1;
	}
	printf("%ux%u, %u bpp, line %u bytes, \"%.16s\"\n", v.xres, v.yres,
	    v.bits_per_pixel, f.line_length, f.id);
	if (v.bits_per_pixel != 32) {
		fprintf(stderr, "fbtest: only 32-bit pixels\n");
		return 1;
	}
	if ((row = malloc(f.line_length)) == NULL)
		return 1;

	for (y = 0; y < v.yres; y++) {
		for (x = 0; x < v.xres; x++) {
			dx = (int)x - (int)v.xres / 2;
			dy = (int)y - (int)v.yres / 2;
			if (strcmp(pattern, "checker") == 0) {
				row[x] = ((x / 32 + y / 32) & 1) ?
				    rgb(&v, 255, 255, 255) : rgb(&v, 0, 0, 0);
			} else {
				d = (dx * dx + dy * dy) / 400;
				row[x] = rgb(&v, d * 7, d * 13, d * 29);
				if (dx == 0 || dy == 0)
					row[x] = rgb(&v, 255, 255, 255);
			}
		}
		if (pwrite(fd, row, v.xres * 4, (off_t)y * f.line_length) !=
		    (ssize_t)(v.xres * 4)) {
			perror("write");
			return 1;
		}
	}
	printf("drawn; holding /dev/fb0 for %u s\n", seconds);
	sleep(seconds);
	close(fd);
	return 0;
}
