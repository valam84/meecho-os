/* Схема test9 целиком, но с печатью jmp_buf перед каждым longjmp: тот
 * падает с "longjmp botch", а сообщение одно на все четыре проверки в
 * __longjmp14 - по нему не видно, какая из них не прошла.
 */
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <machine/setjmp.h>

static jmp_buf env;
static char buf[512];

static void
dump(const char *tag)
{
	unsigned long long *p = (unsigned long long *)(void *)env;

	printf("%-10s magic %016llx sp %016llx x29 %016llx x30 %016llx\n",
	    tag, p[_JB_MAGIC], p[_JB_SP], p[_JB_X29], p[_JB_X30]);
	fflush(stdout);
}

static void level1(void);
static void level2(void);
static void dolev(void);
static void hard(void);

static void
catcher(int s)
{
	(void)s;
	dump("catch");
	longjmp(env, 4);
}

static void
hard(void)
{
	char *p;

	signal(SIGHUP, catcher);
	for (p = buf; p <= &buf[511]; p++)
		*p = 025;
	dump("hard");
	kill(getpid(), SIGHUP);
}

static void
dolev(void)
{
	dump("dolev");
	longjmp(env, 3);
}

static void
level2(void)
{
	dolev();
}

static void
level1(void)
{
	dump("level1");
	longjmp(env, 2);
}

static void
garbage(void)
{
	switch (setjmp(env)) {
	case 0:
		dump("case0");
		longjmp(env, 1);
		break;
	case 1:
		printf("reached case 1\n"); fflush(stdout);
		level1();
		break;
	case 2:
		printf("reached case 2\n"); fflush(stdout);
		level2();
		break;
	case 3:
		printf("reached case 3\n"); fflush(stdout);
		hard();
		/* FALLTHROUGH */
	case 4:
		printf("reached case 4\n"); fflush(stdout);
		return;
	default:
		printf("unexpected value\n");
	}
}

int
main(void)
{
	int i;

	for (i = 0; i < 3; i++) {
		printf("--- round %d\n", i); fflush(stdout);
		garbage();
	}
	printf("all rounds done\n");
	return 0;
}
