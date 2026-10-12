/*
 * kmsg: напечатать кольцевой буфер сообщений ядра - то, что tty выводит на
 * консоль: строки ядра и printf() системных служб (они идут через ядро).
 *
 *   kmsg          что лежит в буфере сейчас
 *   kmsg -f       и дальше всё новое, пока не прервут (как tail -f)
 *
 * Зачем: драйвер, умерший в своей инициализации, успевает напечатать, почему,
 * но только на консоль - а консоль платы на 1.5 Мбод теряет куски строк и
 * забита отладкой сети. Буфер ядра (struct kmessages) отображён в каждый
 * процесс через minix_kerninfo - так его читает сам tty, - и пока его не
 * переписали по кругу, причина лежит там целиком.
 *
 * -f нужен потому, что "пока не переписали" бывает недолго: dwmac с log=3
 * переписывает весь буфер за секунды, и строк USB в нём уже нет к тому
 * времени, как до него дойдёт ssh. Слежение опрашивает km_next раз в 20 мс
 * и забирает новое. Пришедшее между двумя взглядами больше, чем весь буфер,
 * так не увидеть - счётчика у буфера нет (см. ниже); за 20 мс столько не
 * пишет даже dwmac.
 *
 *   aarch64-elf64-minix-gcc -O -static --sysroot=$HOME/dest-evbarm64 \
 *       -o kmsg port/test/fb/kmsg.c
 */
#include <lib.h>			/* get_minix_kerninfo() */
#include <minix/type.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>

int
main(int argc, char *argv[])
{
	struct minix_kerninfo *ki = get_minix_kerninfo();
	struct kmessages *km;
	static char buf[_KMESS_BUF_SIZE];
	int next, size, start, i, last, n, follow;

	follow = argc > 1 && strcmp(argv[1], "-f") == 0;

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
	fflush(stdout);

	/*
	 * km_next is all there is to go by: the buffer has no running count,
	 * so a wrap between two looks is invisible as such.  What can be
	 * seen is km_size, which the kernel caps at the buffer's length -
	 * once it is full, every byte written moves km_next by one, and the
	 * distance is the amount new as long as it is less than a buffer.
	 */
	for (last = next; follow; last = next) {
		usleep(20000);
		next = km->km_next;
		n = (next - last + _KMESS_BUF_SIZE) % _KMESS_BUF_SIZE;
		for (i = 0; i < n; i++)
			buf[i] = km->km_buf[(last + i) % _KMESS_BUF_SIZE];
		if (n > 0) {
			fwrite(buf, 1, n, stdout);
			fflush(stdout);
		}
	}
	return 0;
}
