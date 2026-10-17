/*
 * Firmware files: the CP's microcode (a530_pm4.fw, a530_pfp.fw) and the zap
 * shader (a512_zap.mdt and its .bNN), read from the root.
 *
 * Neither may live in MEECHO's tree (hw-gpu.md, §5): the microcode is
 * linux-firmware's under LICENSE.qcom, the zap shader is signed by the
 * tablet's maker and comes off its own vendor partition.  They are put on
 * the machine beside the service - /libdata/firmware/qcom, where NetBSD's
 * firmload(9) would look and where Linux's /lib/firmware/qcom has them -
 * and the service reads them with plain open(2) and read(2), as RS reads a
 * program to start.  Nothing serves this driver yet, so VFS never waits on
 * it while it waits on VFS.
 *
 *	fwdir=DIR	where they are (default /libdata/firmware/qcom)
 */

#include "adreno.h"

#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

char ad_fwdir[64] = "/libdata/firmware/qcom";

int
fw_read(const char *name, void **buf, size_t *len, size_t max)
{
	char path[128];
	struct stat st;
	ssize_t n;
	size_t got = 0;
	uint8_t *b;
	int fd;

	*buf = NULL;
	*len = 0;
	if (snprintf(path, sizeof(path), "%s/%s", ad_fwdir, name) >=
	    (int)sizeof(path))
		return ENAMETOOLONG;
	if ((fd = open(path, O_RDONLY)) < 0)
		return errno;
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
		close(fd);
		return EINVAL;
	}
	if (st.st_size <= 0 || (size_t)st.st_size > max) {
		close(fd);
		return EFBIG;
	}
	if ((b = malloc((size_t)st.st_size)) == NULL) {
		close(fd);
		return ENOMEM;
	}
	while (got < (size_t)st.st_size) {
		n = read(fd, b + got, (size_t)st.st_size - got);
		if (n <= 0) {
			free(b);
			close(fd);
			return n < 0 ? errno : EIO;
		}
		got += (size_t)n;
	}
	close(fd);
	*buf = b;
	*len = got;
	return OK;
}

/* A checksum to say "the same bytes" in one line: FNV-1a, 32 bits. */
uint32_t
fw_sum(const void *p, size_t n, uint32_t h)
{
	const uint8_t *b = p;

	while (n-- > 0)
		h = (h ^ *b++) * 16777619u;
	return h;
}
