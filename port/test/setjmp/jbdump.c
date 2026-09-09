/* Что на самом деле лежит в jmp_buf после setjmp(3), и куда возвращает
 * longjmp(3).  Написано потому, что test9 падает с "longjmp botch" на
 * простейшем setjmp/longjmp, а сообщение об этом одно на все четыре
 * проверки в __longjmp14 - по нему не видно, какая из них не прошла.
 *
 * Случая четыре, и они различаются тем, откуда зовётся longjmp:
 * из той же функции, из вложенной, из обработчика сигнала и через
 * _setjmp/_longjmp (без маски сигналов) - ash пользуется именно ими.
 */
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <machine/setjmp.h>

static jmp_buf env;
static jmp_buf envs;

static void
dump(const char *tag, jmp_buf b)
{
	unsigned long long *p = (unsigned long long *)(void *)b;

	printf("%s: magic %016llx (want %016llx) sp %016llx x29 %016llx "
	    "x30 %016llx tpidr %016llx\n", tag, p[_JB_MAGIC],
	    (unsigned long long)_JB_MAGIC_AARCH64_SETJMP, p[_JB_SP],
	    p[_JB_X29], p[_JB_X30], p[_JB_TPIDR]);
	fflush(stdout);
}

static void
deeper(void)
{
	longjmp(env, 2);
}

static void
catcher(int sig)
{
	(void)sig;
	dump("in handler", env);
	longjmp(env, 3);
}

int
main(void)
{
	int r;

	/* 1. longjmp из той же функции */
	if ((r = setjmp(env)) == 0) {
		dump("same frame", env);
		longjmp(env, 1);
	}
	printf("same frame: returned %d\n", r);

	/* 2. longjmp из вложенного вызова */
	if ((r = setjmp(env)) == 0) {
		dump("nested", env);
		deeper();
	}
	printf("nested: returned %d\n", r);

	/* 3. longjmp из обработчика сигнала */
	signal(SIGUSR1, catcher);
	if ((r = setjmp(env)) == 0) {
		dump("from handler", env);
		kill(getpid(), SIGUSR1);
		printf("from handler: signal did not arrive\n");
	}
	printf("from handler: returned %d\n", r);

	/* 4. _setjmp/_longjmp - то, чем пользуется ash */
	if ((r = _setjmp(envs)) == 0) {
		dump("_setjmp", envs);
		_longjmp(envs, 4);
	}
	printf("_setjmp: returned %d\n", r);

	printf("all four done\n");
	return 0;
}
