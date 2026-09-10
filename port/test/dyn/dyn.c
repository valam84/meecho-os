/*
 * The acceptance test for dynamic linking: one program that reaches main()
 * through ld.elf_so and then makes the loader do its remaining work.
 *
 * Every call below is a PLT call the first time it runs, so the program is
 * really a test of _rtld_bind_start: the lazy path resolves each symbol on
 * its first use.  The double argument to printf is not decoration - the
 * arguments of a lazily bound call travel in V0..V7 as well as X0..X7, and
 * a resolver that does not save them corrupts exactly this case and nothing
 * else.  Run it a second time with LD_BIND_NOW set to take the other path,
 * where every slot is resolved before main() is entered.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int counter;

static void
bye(void)
{
	printf("atexit ran, counter %d\n", counter);
}

int
main(int argc, char **argv)
{
	char *heap;
	const char *path;

	printf("argc %d argv0 %s\n", argc, argv[0]);

	heap = malloc(4096);
	if (heap == NULL) {
		printf("malloc failed\n");
		return 1;
	}
	memset(heap, 'x', 4096);
	heap[4095] = '\0';
	printf("malloc ok, strlen %zu\n", strlen(heap));
	free(heap);

	/* Floating point through a variadic PLT call. */
	printf("double %.3f, sum %.3f\n", 2.5, 2.5 + 0.25);

	path = getenv("PATH");
	printf("PATH %s\n", path != NULL ? path : "(unset)");
	printf("pid %d uid %d\n", (int)getpid(), (int)getuid());

	counter = argc + 41;
	atexit(bye);
	printf("dynamic hello ok\n");
	return 0;
}
