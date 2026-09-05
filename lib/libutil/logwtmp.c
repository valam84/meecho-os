/*	$NetBSD: logwtmp.c,v 1.14 2003/08/07 16:44:59 agc Exp $	*/

/*
 * Copyright (c) 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
#if defined(LIBC_SCCS) && !defined(lint)
#if 0
static char sccsid[] = "@(#)logwtmp.c	8.1 (Berkeley) 6/4/93";
#else
__RCSID("$NetBSD: logwtmp.c,v 1.14 2003/08/07 16:44:59 agc Exp $");
#endif
#endif /* LIBC_SCCS and not lint */

#include <sys/types.h>
#include <sys/file.h>
#include <sys/time.h>
#include <sys/stat.h>

#include <assert.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <utmp.h>
#include <sys/param.h>
#include <util.h>

void
logwtmp(const char *line, const char *name, const char *host)
{
	struct utmp ut;
	struct stat buf;
	int fd;

	_DIAGASSERT(line != NULL);
	_DIAGASSERT(name != NULL);
	_DIAGASSERT(host != NULL);

	if ((fd = open(_PATH_WTMP, O_WRONLY|O_APPEND, 0)) < 0)
		return;
	if (fstat(fd, &buf) == 0) {
		/*
		 * Fixed-width utmp fields, not C strings: NUL-padded, and
		 * allowed to fill the field completely. That is exactly what
		 * strncpy does and why it was used; GCC cannot tell a
		 * deliberately unterminated field from an oversight, so the
		 * two halves are written out.
		 */
		(void) memset(ut.ut_line, 0, sizeof(ut.ut_line));
		(void) memcpy(ut.ut_line, line,
		    MIN(strlen(line), sizeof(ut.ut_line)));
		(void) memset(ut.ut_name, 0, sizeof(ut.ut_name));
		(void) memcpy(ut.ut_name, name,
		    MIN(strlen(name), sizeof(ut.ut_name)));
		(void) memset(ut.ut_host, 0, sizeof(ut.ut_host));
		(void) memcpy(ut.ut_host, host,
		    MIN(strlen(host), sizeof(ut.ut_host)));
		(void) time(&ut.ut_time);
		if (write(fd, &ut, sizeof(struct utmp)) != sizeof(struct utmp))
			(void) ftruncate(fd, buf.st_size);
	}
	(void) close(fd);
}
