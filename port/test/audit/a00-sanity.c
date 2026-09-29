/*
 * The harness itself: after audit_drop() the probe is not root - it cannot
 * read /etc/master.passwd or signal init. Exit 0 means the drop works and
 * every other probe really runs as an ordinary user.
 */
#include <signal.h>
#include "audit.h"

int
main(void)
{
	audit_drop();
	if (open("/etc/master.passwd", O_RDONLY) >= 0)
		errx(1, "read /etc/master.passwd as uid %d", (int)getuid());
	if (kill(1, SIGTERM) == 0)
		errx(1, "signalled init as uid %d", (int)getuid());
	printf("uid %d gid %d: unprivileged\n", (int)getuid(), (int)getgid());
	return 0;
}
