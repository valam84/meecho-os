/*
 * RESOURCE: fill the process table. Children sleep; the parent forks until
 * fork() fails, which must be EAGAIN - not a panic in PM or VM. Then all
 * children are killed and reaped, and fork() must work again.
 */
#include <signal.h>
#include <sys/wait.h>
#include "audit.h"

int
main(void)
{
	int n = 0, e;
	pid_t p;

	audit_drop();
	for (;;) {
		if ((p = fork()) < 0) { e = errno; break; }
		if (p == 0) { pause(); _exit(0); }
		n++;
	}
	printf("forked %d, then: %s\n", n, strerror(e));
	signal(SIGTERM, SIG_IGN);	/* kill(0) reaches this process too */
	kill(0, SIGTERM);
	while (wait(NULL) > 0 || errno == EINTR)
		;
	if ((p = fork()) < 0)
		err(1, "fork after cleanup");
	if (p == 0) _exit(0);
	wait(NULL);
	return e == EAGAIN ? 0 : 1;
}
