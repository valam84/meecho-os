/*
 * Common part of the server-audit probes (port/test/audit/).
 *
 * Each probe is one attempt to make a non-restartable server (VFS, PM, VM,
 * RS, sched) panic or fail an assertion from an ordinary user process. It
 * gives up root first thing, so whatever it manages is something any user
 * can do. The runner (run.py) decides the outcome by whether the system is
 * still alive afterwards, not by what the probe prints: a probe that brings
 * the machine down never gets to print anything.
 */
#ifndef AUDIT_H
#define AUDIT_H

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <grp.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define AUDIT_UID	9999
#define AUDIT_GID	9999

/* Stop being root. Called before anything else. */
static void
audit_drop(void)
{
	gid_t g = AUDIT_GID;

	if (setgroups(1, &g) != 0 || setgid(AUDIT_GID) != 0 ||
	    setuid(AUDIT_UID) != 0)
		err(2, "cannot drop privileges");
	if (getuid() != AUDIT_UID || geteuid() != AUDIT_UID)
		errx(2, "still privileged");
}

/* A working directory the unprivileged user can write to. */
static void
audit_tmpdir(const char *name)
{
	char path[256];

	snprintf(path, sizeof(path), "/tmp/audit.%s.%d", name, (int)getpid());
	if (mkdir(path, 0700) != 0 || chdir(path) != 0)
		err(2, "%s", path);
}

#endif
