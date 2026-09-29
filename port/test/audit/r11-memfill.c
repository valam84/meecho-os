/*
 * RESOURCE: take all memory, from several processes at once, touching
 * every page. Allocation must fail (NULL, or SIGSEGV/SIGKILL for the
 * hog) - VM, VFS and PM must stay up. Afterwards the system must still
 * fork and allocate.
 */
#include <signal.h>
#include <sys/wait.h>
#include "audit.h"

#define HOGS	4
#define CHUNK	(1 << 20)

int
main(void)
{
	int i, st;
	pid_t p;

	audit_drop();
	for (i = 0; i < HOGS; i++) {
		if ((p = fork()) < 0) err(1, "fork");
		if (p == 0) {
			size_t got = 0;
			char *c;
			while ((c = malloc(CHUNK)) != NULL) {
				memset(c, 0x5a, CHUNK);
				got += CHUNK;
			}
			printf("hog %d: %zu MB\n", i, got >> 20);
			_exit(0);
		}
	}
	for (i = 0; i < HOGS; i++) {
		wait(&st);
		if (WIFSIGNALED(st))
			printf("hog ended by signal %d\n", WTERMSIG(st));
	}
	if ((p = fork()) < 0) err(1, "fork afterwards");
	if (p == 0) { free(malloc(CHUNK)); _exit(0); }
	wait(NULL);
	return 0;
}
