/*
 * RESOURCE: exhaust VFS's global open-file table from many processes
 * (each stops at its own OPEN_MAX), then pipes (vnodes). Expected:
 * EMFILE/ENFILE, no panic. Everything is released by exit.
 */
#include <signal.h>
#include <sys/wait.h>
#include "audit.h"

#define PROCS	24

int
main(void)
{
	int i, st, fds[2];
	pid_t p;

	audit_drop();
	audit_tmpdir("fd");
	for (i = 0; i < PROCS; i++) {
		if ((p = fork()) < 0) { warn("fork %d", i); break; }
		if (p == 0) {
			int n = 0, e = 0;
			char name[32];
			for (;;) {
				if (n % 2) {
					if (pipe(fds) != 0) { e = errno; break; }
					n += 2;
				} else {
					snprintf(name, sizeof(name), "f%d.%d", i, n);
					if (open(name, O_CREAT | O_RDWR, 0600) < 0)
						{ e = errno; break; }
					n++;
				}
			}
			if (i == 0 || e != EMFILE)
				printf("proc %d: %d fds, then %s\n", i, n,
				    strerror(e));
			sleep(2);
			_exit(0);
		}
	}
	while (wait(&st) > 0)
		;
	return 0;
}
