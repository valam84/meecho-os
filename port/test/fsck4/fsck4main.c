/*
 * A main() for running the V4 checker on the build host, against an image
 * file.  The checker itself uses nothing but pread/pwrite and stdio, so it
 * compiles and behaves the same here as it does in the system - which is
 * what makes it possible to try it on a hundred deliberately damaged
 * images without booting anything.
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "fsck4.h"
#include "exitvalues.h"

/* fsck.c defines this one itself; exitvalues.h does not have it. */
#ifndef FSCK_EXIT_UNKNOWN_FS
#define FSCK_EXIT_UNKNOWN_FS	16
#endif

int main(int argc, char **argv)
{
	int fd, flags = 0, i, ro = 0;
	const char *name = NULL;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-a") || !strcmp(argv[i], "-y"))
			flags |= FSCK4_REPAIR | FSCK4_AUTO;
		else if (!strcmp(argv[i], "-p"))
			flags |= FSCK4_REPAIR | FSCK4_AUTO | FSCK4_PREEN;
		else if (!strcmp(argv[i], "-n"))
			ro = 1;
		else if (!strcmp(argv[i], "-l"))
			flags |= FSCK4_LISTING;
		else
			name = argv[i];
	}

	if (name == NULL) {
		fprintf(stderr, "usage: %s [-a|-p|-n] [-l] image\n", argv[0]);
		return FSCK_EXIT_USAGE;
	}

	if ((fd = open(name, ro ? O_RDONLY : O_RDWR)) < 0) {
		perror(name);
		return FSCK_EXIT_CHECK_FAILED;
	}

	if (!fsck4_is_v4(fd)) {
		fprintf(stderr, "%s: not a V4 file system\n", name);
		return FSCK_EXIT_UNKNOWN_FS;
	}

	i = fsck4_check(fd, name, flags);

	close(fd);

	return i;
}
