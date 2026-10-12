/*
 * kmsg: напечатать кольцевой буфер сообщений ядра - то, что tty выводит на
 * консоль: строки ядра и printf() системных служб (они идут через ядро).
 *
 *   kmsg
 *
 * Зачем: драйвер, умерший в своей инициализации, успевает напечатать, почему,
 * но только на консоль - а консоль платы на 1.5 Мбод теряет куски строк и
 * забита отладкой сети. Буфер ядра (struct kmessages) отображён в каждый
 * процесс через minix_kerninfo - так его читает сам tty, - и пока его не
 * переписали по кругу, причина лежит там целиком.
 *
 *   aarch64-elf64-minix-gcc -O -static --sysroot=$HOME/dest-evbarm64 \
 *       -o kmsg port/test/fb/kmsg.c
 */
#include <lib.h>			/* get_minix_kerninfo() */
#include <minix/type.h>

#include <stdio.h>
#include <string.h>

int
main(void)
{
	struct minix_kerninfo *ki = get_minix_kerninfo();
	struct kmessages *km;
	static char buf[_KMESS_BUF_SIZE];
	int next, size, start, i;

	if (ki == NULL || (km = ki->kmessages) == NULL) {
		fprintf(stderr, "kmsg: no kernel message buffer\n");
		return 1;
	}
	next = km->km_next;
	size = km->km_size;
	if (size > _KMESS_BUF_SIZE)
		size = _KMESS_BUF_SIZE;
	start = (next - size + _KMESS_BUF_SIZE) % _KMESS_BUF_SIZE;
	for (i = 0; i < size; i++)
		buf[i] = km->km_buf[(start + i) % _KMESS_BUF_SIZE];
	fwrite(buf, 1, size, stdout);
	return 0;
}
