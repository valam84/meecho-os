/* test9 падает на самом простом блоке из всех - setjmp/longjmp в main с
 * jmp_buf на стеке.  С глобальным jmp_buf тот же код работает, поэтому
 * здесь проверяются оба и печатается выравнивание буфера.
 */
#include <setjmp.h>
#include <stdio.h>
#include <machine/setjmp.h>

static jmp_buf genv;

static void
dump(const char *tag, jmp_buf b)
{
	unsigned long long *p = (unsigned long long *)(void *)b;

	printf("%-8s at %p (align %lu) magic %016llx sp %016llx x29 %016llx "
	    "x30 %016llx\n", tag, (void *)p,
	    (unsigned long)((unsigned long)p & 15),
	    p[_JB_MAGIC], p[_JB_SP], p[_JB_X29], p[_JB_X30]);
	fflush(stdout);
}

int
main(void)
{
	jmp_buf lenv;
	int i, r;

	printf("sizeof(jmp_buf) = %lu\n", (unsigned long)sizeof(lenv));

	i = 1;
	if ((r = setjmp(lenv)) == 0) {
		dump("local", lenv);
		i = 2;
		longjmp(lenv, 1);
	}
	printf("local: returned %d, i = %d\n", r, i);

	if ((r = setjmp(genv)) == 0) {
		dump("global", genv);
		longjmp(genv, 1);
	}
	printf("global: returned %d\n", r);
	return 0;
}
