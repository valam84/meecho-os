/*
 * pmagic.c - где на самом деле лежит p_magic в таблице процессов ядра.
 *
 * Зачем. trace(1) на этом порте отказывается работать: "Kernel magic check
 * failed". Разделённое сообщение показало, что чтение УДАЁТСЯ, но по
 * смещению, которое trace вычислил из своей копии kernel/proc.h, лежит ноль,
 * а не PMAGIC. То есть trace и ядро по-разному раскладывают struct proc.
 *
 * Эта программа не спорит с заголовками, а спрашивает ядро: перебирает
 * смещения через T_GETUSER и печатает те, где лежит PMAGIC. Разница между
 * найденным смещением и тем, что вычислил trace, и есть размер расхождения -
 * дальше его ищут в объявлении структуры.
 *
 * Собирается кросс-компилятором против DESTDIR; на плату кладётся
 * port/board-cc.sh, на QEMU - через образ корня.
 */
#include <sys/types.h>
#include <sys/ptrace.h>
#include <sys/wait.h>

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Из minix/kernel/proc.h. Здесь числом, чтобы программа не зависела от
 * заголовков ядра - именно про них и вопрос. */
#define PMAGIC		0xc0ffee1

/* Насколько далеко смотреть. struct proc заведомо меньше. */
#define MAX_OFF		8192

int
main(int argc, char **argv)
{
	long v;
	pid_t pid;
	int status, off, step, found = 0, limit;

	int target = 0, own_child;

	/* -p pid: целиться в чужой процесс, как это делает trace -p. Свой
	 * ребёнок и чужой процесс проходят через ptrace разные пути, и
	 * разница между ними - как раз то, что здесь выясняется. */
	if (argc > 2 && argv[1][0] == '-' && argv[1][1] == 'p') {
		target = atoi(argv[2]);
		argc -= 2;
		argv += 2;
	}
	limit = (argc > 1) ? atoi(argv[1]) : MAX_OFF;
	step = (int)sizeof(long);

	if (target > 0) {
		pid = (pid_t)target;
		own_child = 0;
	} else {
		own_child = 1;
		if ((pid = fork()) == -1) {
			perror("fork");
			return 1;
		}
		if (pid == 0) {
			/* Ребёнок только ждёт: его таблицу и читаем. */
			for (;;)
				pause();
			_exit(0);
		}
	}
	printf("цель: pid %d (%s)\n", (int)pid,
	    own_child ? "свой ребёнок" : "чужой процесс");

	if (ptrace(T_ATTACH, pid, 0, 0) != 0) {
		perror("T_ATTACH");
		kill(pid, SIGKILL);
		return 1;
	}
	if (waitpid(pid, &status, 0) != pid) {
		perror("waitpid");
		kill(pid, SIGKILL);
		return 1;
	}

	printf("sizeof(long) = %d, шаг %d, ищем 0x%x до смещения %d\n",
	    (int)sizeof(long), step, PMAGIC, limit);

	for (off = 0; off + step <= limit; off += step) {
		errno = 0;
		v = ptrace(T_GETUSER, pid, (void *)(long)off, 0);
		if (errno != 0) {
			printf("смещение %d: чтение отказало (%s) - дальше "
			    "таблица кончилась\n", off, strerror(errno));
			break;
		}
		/* Магия - 32-битное поле, поэтому смотрим обе половины
		 * машинного слова: выравнивание поля заранее неизвестно. */
		if ((unsigned int)(v & 0xffffffffu) == PMAGIC) {
			printf("PMAGIC по смещению %d (младшая половина "
			    "слова %d)\n", off, off);
			found++;
		}
		if (step == 8 &&
		    (unsigned int)((unsigned long)v >> 32) == PMAGIC) {
			printf("PMAGIC по смещению %d (старшая половина "
			    "слова %d)\n", off + 4, off);
			found++;
		}
	}

	if (found == 0)
		printf("PMAGIC не найден вовсе: читается не таблица "
		    "процессов, либо магия другая\n");

	/* И ещё раз то же самое место, но точно тем способом, которым это
	 * делает trace(1): выровненное вниз слово плюс memcpy нужной
	 * половины. Если здесь выйдет не то же самое, виноват этот способ, а
	 * не раскладка. */
	{
		unsigned long addr = 1156;
		unsigned long aligned = addr & ~(unsigned long)(sizeof(long) - 1);
		unsigned int field = 0;

		errno = 0;
		v = ptrace(T_GETUSER, pid, (void *)aligned, 0);
		if (errno != 0) {
			printf("как в trace: чтение по %lu отказало (%s)\n",
			    aligned, strerror(errno));
		} else {
			memcpy(&field, (char *)&v + (addr - aligned),
			    sizeof(field));
			printf("как в trace: слово по %lu = 0x%016lx, "
			    "поле по %lu = 0x%08x\n", aligned,
			    (unsigned long)v, addr, field);
		}
	}

	(void)ptrace(T_DETACH, pid, 0, 0);
	if (own_child) {
		kill(pid, SIGKILL);
		(void)waitpid(pid, &status, 0);
	}
	return found == 0;
}
