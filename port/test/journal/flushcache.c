/*
 * flushcache: F_FLUSH_FS_CACHE in the middle of an open journal transaction.
 *
 * Each round creates a file in the current directory (a metadata change,
 * so its blocks join the open transaction and are pinned in the cache),
 * then asks the file system to flush and purge its cache through
 * fcntl(F_FLUSH_FS_CACHE), as test74 does between its experiments.  Sixteen
 * operations later the journal commits.
 *
 * The purge used to unmap the pinned blocks too.  Two things followed: the
 * metadata changes they carried were dropped without ever reaching the disk
 * (files created a moment ago are gone), and the next commit read the
 * journal's pointer to a block whose memory was gone - MFS died on a NULL
 * read in the checksum loop, and with the root file system dead the system
 * hung.  See port/PORTING-LOG.md, "Тест 80 после прерванного теста 74".
 *
 * Run as root (F_FLUSH_FS_CACHE is for the superuser) in a directory on a
 * journaled MFS V4 volume.  Exit 0: every file survived; 1: some were lost.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define ROUNDS	48

int
main(void)
{
	char name[32];
	struct stat st;
	int i, fd, lost = 0;

	for (i = 0; i < ROUNDS; i++) {
		snprintf(name, sizeof(name), "fc.%d", i);
		if ((fd = open(name, O_RDWR | O_CREAT | O_TRUNC, 0644)) < 0) {
			perror(name);
			return 2;
		}
		if (write(fd, "x", 1) != 1) {
			perror("write");
			return 2;
		}
		if (fcntl(fd, F_FLUSH_FS_CACHE) < 0) {
			perror("F_FLUSH_FS_CACHE");
			return 2;
		}
		close(fd);
	}

	for (i = 0; i < ROUNDS; i++) {
		snprintf(name, sizeof(name), "fc.%d", i);
		if (stat(name, &st) < 0) {
			printf("lost %s: %s\n", name, strerror(errno));
			lost++;
			continue;
		}
		unlink(name);
	}

	printf("flushcache: %d rounds, %d files lost\n", ROUNDS, lost);
	return lost ? 1 : 0;
}
