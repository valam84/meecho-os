/*
 * errnosign - the sign of errno after a libc call fails.
 *
 *   errnosign
 *
 * A libc file built with "#define _SYSTEM" sees the error codes of
 * <errno.h> as the servers' negative ones (_SIGN is "-"), while errno in the
 * process is positive - _syscall() stores -m_type.  So in such a file
 * "errno = ENOSYS" stores -78 and "errno == ENODEV" is never true.  Found
 * 2026-10-14 in mmap(2) (stage 11.6); the same shape was left in the
 * SysV IPC calls, which set ENOSYS when the "ipc" service cannot be found.
 *
 * This is built WITHOUT _SYSTEM, so ENOSYS here is the user's +78, and every
 * check is the user's own view: after a call that fails, errno must be
 * positive and, where the cause is known, the right one.
 *
 *   semget semctl semop shmget shmat shmdt shmctl   no "ipc" service on this
 *       port (there is none in /service): ENOSYS, as every one of them says
 *   open close mmap(/dev/null)   controls: errno that _syscall() or the
 *       driver half of mmap(2) set from a server's reply
 *
 * Each check prints "ok" or "FAIL" with the errno seen; the last line is
 *
 *   ERRNOSIGN checks N failed F
 */
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/mman.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int checks, fails;

/*
 * A call that failed leaves errno positive; the number must also be the
 * expected one when "want" is not zero.
 */
static void
check(const char *name, int failed, int want)
{
	int e = errno;
	int ok = failed && e > 0 && (want == 0 || e == want);

	checks++;
	if (!ok)
		fails++;
	printf("%s  %-14s %s errno %d (%s)", ok ? "ok  " : "FAIL", name,
	    failed ? "failed," : "DID NOT FAIL,", e,
	    e > 0 ? strerror(e) : "negative: not an error number");
	if (want)
		printf("; expected %d", want);
	printf("\n");
}

int
main(void)
{
	/* With an "ipc" service these fail with whatever it says. */
	int ipc = access("/service/ipc", X_OK) == 0;
	int want = ipc ? 0 : ENOSYS;
	struct sembuf sb;
	int fd;
	void *p;

	printf("ENOSYS here is %d, ENODEV %d, ENOENT %d\n",
	    ENOSYS, ENODEV, ENOENT);
	printf("ipc service: %s\n", ipc ? "present" : "absent");

	memset(&sb, 0, sizeof(sb));

	errno = 0;
	check("semget", semget(IPC_PRIVATE, 1, IPC_CREAT | 0600) == -1, want);
	errno = 0;
	check("semctl", semctl(0, 0, IPC_RMID) == -1, want);
	errno = 0;
	check("semop", semop(0, &sb, 1) == -1, want);
	errno = 0;
	check("shmget", shmget(IPC_PRIVATE, 4096, IPC_CREAT | 0600) == -1, want);
	errno = 0;
	check("shmat", shmat(0, NULL, 0) == (void *)-1, want);
	errno = 0;
	check("shmdt", shmdt((void *)0x1000) == -1, want);
	errno = 0;
	check("shmctl", shmctl(0, IPC_RMID, NULL) == -1, want);

	errno = 0;
	check("open", open("/no/such/file", O_RDONLY) == -1, ENOENT);
	errno = 0;
	check("close", close(-1) == -1, EBADF);

	/* VM refuses a character device, the driver is asked: /dev/null has no mmap. */
	fd = open("/dev/null", O_RDWR);
	if (fd < 0) {
		checks++;
		fails++;
		printf("FAIL  open /dev/null: errno %d\n", errno);
	} else {
		errno = 0;
		p = mmap(NULL, 4096, PROT_READ, MAP_SHARED, fd, 0);
		check("mmap /dev/null", p == MAP_FAILED, ENODEV);
		close(fd);
	}

	printf("ERRNOSIGN checks %d failed %d\n", checks, fails);
	return fails != 0;
}
