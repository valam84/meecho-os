/*
 * meechowm -b status|load [MS]: the panel's two script modules (yambar,
 * meechowm-bar.sh) as one process each that stays, instead of a script
 * yambar starts again every second.
 *
 * Why.  The panel's modules were "script" modules with a poll interval:
 * every second yambar ran "meechowm-bar status" - sh, "meechowm -m
 * state" and awk - and every two "meechowm-bar load" - sh, sysctl, tr,
 * cat and awk.  Five or six fork and exec a second of static programs,
 * on an idle desktop where nothing changed: on the Mi Pad 4 that was VM
 * at 15 % of the boot processor and most of what the idle desktop cost
 * (port/clover/hw-cpu.md, "Фон на ядре 0").  yambar's script module also
 * takes a script that never ends and writes a tag set whenever it likes,
 * an empty line after each; so this one asks what it would have asked,
 * every MS milliseconds (1000 for status, 2000 for load), and writes only
 * what differs from what it wrote last - an unchanged tag set would only
 * make yambar draw the same panel again.
 *
 *   status   the workspaces, a dot each and the one on the screen filled,
 *            and the focused window's title: from "state" over the
 *            compositor's socket (ipc.c), as tag|type|value lines
 *   load     how busy the processors have been since the last look
 *            (kern.cp_time) and the memory in use in MiB (/proc/meminfo:
 *            what is neither free nor cache)
 *
 * MS 0 is once, as "meechowm-bar status" and "load" print for a person or
 * a stand; then load keeps its last look in $XDG_RUNTIME_DIR, as the script
 * did, so that two runs in a row say something.  The loop ends when yambar
 * does: a write to the pipe it read fails.
 */
#include <sys/types.h>
#include <sys/sysctl.h>

#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "wm.h"

#define NSTATES	5		/* kern.cp_time: user nice sys intr idle */

static char out[2048], last[2048];

/* The status tag set, from the compositor's "state" (strip.c, state()). */
static int
status(void)
{
	static char st[32768];
	char *line, *next, *p, title[512] = "";
	int i, n = 0, a = 0, len, wi, ci;
	size_t o;

	if ((len = ipc_query("state\n", st, sizeof(st))) < 0)
		return -1;
	st[len] = '\0';
	for (line = st; line != NULL && *line != '\0'; line = next) {
		if ((next = strchr(line, '\n')) != NULL)
			*next++ = '\0';
		if (strncmp(line, "screen ", 7) == 0) {
			if ((p = strstr(line, " workspaces ")) != NULL)
				n = atoi(p + 12);
			if ((p = strstr(line, " active ")) != NULL)
				a = atoi(p + 8);
		} else if (sscanf(line, "column %d.%d", &wi, &ci) == 2 &&
		    wi == a && (p = strstr(line, " focus ")) != NULL &&
		    (p = strstr(p, " title ")) != NULL)
			strlcpy(title, p + 7, sizeof(title));
	}
	o = strlcpy(out, "workspace|string|", sizeof(out));
	for (i = 1; i <= n && o < sizeof(out) - 8; i++) {
		if (i > 1)
			out[o++] = ' ';
		/* U+25CF and U+25CB, in UTF-8 */
		memcpy(out + o, i == a ? "\342\227\217" : "\342\227\213", 3);
		o += 3;
	}
	out[o] = '\0';
	snprintf(out + o, sizeof(out) - o, "\ntitle|string|%s\n\n", title);
	return 0;
}

static int
cp_time(uint64_t t[NSTATES])
{
	size_t len = NSTATES * sizeof(uint64_t);

	return sysctlbyname("kern.cp_time", t, &len, NULL, 0) == 0 &&
	    len == NSTATES * sizeof(uint64_t) ? 0 : -1;
}

/* The load tag set; was[] the last look, or all zero for none. */
static void
load(uint64_t was[NSTATES])
{
	uint64_t now[NSTATES], busy, all, busy0, all0;
	unsigned long ps, total, free, largest, cached;
	long mem = 0;
	int cpu = 0, i;
	FILE *f;

	if (cp_time(now) == 0) {
		busy = now[0] + now[1] + now[2] + now[3];
		all = busy + now[4];
		busy0 = was[0] + was[1] + was[2] + was[3];
		all0 = busy0 + was[4];
		if (all0 != 0 && all > all0)
			cpu = (int)((200 * (busy - busy0) / (all - all0) + 1) /
			    2);
		for (i = 0; i < NSTATES; i++)
			was[i] = now[i];
	}
	/* page size, pages: total, free, largest free run, cache */
	if ((f = fopen("/proc/meminfo", "r")) != NULL) {
		if (fscanf(f, "%lu %lu %lu %lu %lu", &ps, &total, &free,
		    &largest, &cached) == 5)
			mem = (long)((total - free - cached) * ps / 1048576);
		fclose(f);
	}
	snprintf(out, sizeof(out), "cpu|int|%d\nmem|int|%ld\n\n", cpu, mem);
}

/* "load" once: the last look kept where the script kept it. */
static void
load_once(void)
{
	uint64_t was[NSTATES] = { 0 };
	const char *dir = getenv("XDG_RUNTIME_DIR");
	char path[256];
	FILE *f;
	int i;

	snprintf(path, sizeof(path), "%s/meechowm-bar.cpu",
	    dir != NULL ? dir : "/tmp");
	if ((f = fopen(path, "r")) != NULL) {
		for (i = 0; i < NSTATES; i++)
			if (fscanf(f, "%" SCNu64, &was[i]) != 1)
				break;
		if (i < NSTATES)
			memset(was, 0, sizeof(was));
		fclose(f);
	}
	load(was);
	if ((f = fopen(path, "w")) != NULL) {
		for (i = 0; i < NSTATES; i++)
			fprintf(f, "%" PRIu64 "%c", was[i],
			    i < NSTATES - 1 ? ' ' : '\n');
		fclose(f);
	}
}

static void
pause_ms(long ms)
{
	struct timespec ts;

	ts.tv_sec = ms / 1000;
	ts.tv_nsec = (ms % 1000) * 1000000;
	while (nanosleep(&ts, &ts) < 0 && errno == EINTR)
		;
}

int
bar_run(int argc, char **argv)
{
	uint64_t was[NSTATES] = { 0 };
	int is_status;
	long ms;

	if (argc < 1 || argc > 2 || (strcmp(argv[0], "status") != 0 &&
	    strcmp(argv[0], "load") != 0)) {
		fprintf(stderr, "usage: meechowm -b status|load [ms]\n");
		return 2;
	}
	is_status = strcmp(argv[0], "status") == 0;
	ms = argc > 1 ? strtol(argv[1], NULL, 10) : is_status ? 1000 : 2000;

	if (ms <= 0) {
		if (is_status) {
			if (status() < 0)
				return 1;
		} else
			load_once();
		fputs(out, stdout);
		return 0;
	}

	/* yambar gone: the write fails rather than the signal ending us. */
	(void)signal(SIGPIPE, SIG_IGN);
	if (!is_status)
		(void)cp_time(was);	/* so that the first one says something */
	for (;;) {
		if (!is_status)
			pause_ms(ms);
		if (is_status ? status() == 0 : (load(was), 1)) {
			if (strcmp(out, last) != 0) {
				if (fputs(out, stdout) == EOF ||
				    fflush(stdout) == EOF)
					return 0;
				strlcpy(last, out, sizeof(last));
			}
		}
		if (is_status)
			pause_ms(ms);
	}
}
