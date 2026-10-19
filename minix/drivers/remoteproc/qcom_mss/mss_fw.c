/*
 * The modem's firmware files, read whole from fwdir (mss.c) with open(2)
 * and read(2), as RS reads a service's binary.
 */

#include "mss.h"

#include <sys/stat.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

char mss_fwdir[64] = "/libdata/firmware/qcom/clover/modem";

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
	if (snprintf(path, sizeof(path), "%s/%s", mss_fwdir, name) >=
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
