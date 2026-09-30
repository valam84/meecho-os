/*
 * Load for the driver-restart test (peer-restart.sh): pairs of UNIX domain
 * sockets passing data back and forth, and closing and reopening, for as
 * long as it is told to run. When uds is restarted underneath, calls fail
 * with an error and the loop starts a fresh pair; what must not happen is
 * the system going down or this process hanging for good.
 *
 * Usage: l20-udsload seconds
 */
#include <sys/socket.h>
#include <time.h>
#include "audit.h"

int
main(int argc, char **argv)
{
	time_t end = time(NULL) + (argc > 1 ? atoi(argv[1]) : 30);
	unsigned long rounds = 0, errors = 0;
	char buf[512];
	int sv[2];

	audit_drop();
	while (time(NULL) < end) {
		if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
			errors++;
			sleep(1);
			continue;
		}
		for (int i = 0; i < 50; i++) {
			memset(buf, i, sizeof(buf));
			if (write(sv[0], buf, sizeof(buf)) != sizeof(buf) ||
			    read(sv[1], buf, sizeof(buf)) <= 0) {
				errors++;
				break;
			}
		}
		close(sv[0]);
		close(sv[1]);
		rounds++;
	}
	printf("udsload: %lu rounds, %lu errors\n", rounds, errors);
	return 0;
}
