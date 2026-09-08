#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

/*
 * MEECHO: прежняя версия возвращала -1, если не удалось закрыть НИ ОДНОГО
 * дескриптора, - то есть отказывала ровно в том случае, когда делать было
 * нечего. У программы, только что запущенной с открытыми 0, 1 и 2, выше
 * второго нет ничего, closefrom(3) закрывает ноль дескрипторов и сообщает
 * EBADF. Нашлось на ssh(1): OpenSSH зовёт closefrom(STDERR_FILENO + 1)
 * первым делом в main() и считает отказ смертельным - "closefrom failed:
 * Bad file descriptor" и выход, ещё до разбора аргументов.
 *
 * Правильное поведение: закрыть всё, что открыто, начиная с fd, и считать
 * EBADF на неоткрытом дескрипторе обычным делом. Отказ остаётся один - сам
 * fd бессмысленный.
 */
int closefrom(int fd)
{
	int f;

	if (fd < 0) {
		errno = EBADF;
		return -1;
	}

	for (f = fd; f < OPEN_MAX; f++)
		(void)close(f);

	return 0;
}
