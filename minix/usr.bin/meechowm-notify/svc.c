/*
 * The look at /proc/service.  Each file there is a service RS has in its
 * table and running; it says, among other things,
 *
 *   endpoint: 73
 *   pid:      45
 *   restarts: 1
 *
 * A look compares that with the last one.  A service whose endpoint or
 * count moved has been restarted (RS gives the new instance an endpoint of
 * its own and counts it in end_srv_init() or reincarnate_service()).  A
 * service missing from GONE_LOOKS looks in a row has stopped: RS gave up
 * on it, or it was taken down.  One missing look is not enough, because a
 * look may fall into a restart - the old instance out of RS's table, the
 * new one not yet in - and the service is then back on the next look with
 * a new endpoint, which is a restart, not a stop.
 *
 * The first look only learns: what was restarted before this program
 * started is not news.
 */
#include <sys/types.h>

#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "notify.h"

#define MAXSVC		128
#define GONE_LOOKS	3

struct svc {
	char label[NT_LABEL];
	int endpoint, pid, restarts;
	unsigned seen;		/* the look it was last there */
	bool gone;		/* said to have stopped */
};

static struct svc svcs[MAXSVC];
static int nsvcs;
static unsigned looks;

static int
read_file(const char *path, char *buf, size_t size)
{
	ssize_t r;
	size_t n = 0;
	int fd;

	if ((fd = open(path, O_RDONLY)) < 0)
		return -1;
	while (n + 1 < size && (r = read(fd, buf + n, size - 1 - n)) > 0)
		n += (size_t)r;
	buf[n] = '\0';
	close(fd);
	return (int)n;
}

static int
field(const char *buf, const char *name, int dflt)
{
	const char *p = strstr(buf, name);

	return p != NULL ? atoi(p + strlen(name)) : dflt;
}

static void
event(struct nt_event *ev, enum nt_kind kind, const struct svc *s, int times)
{
	ev->kind = kind;
	strlcpy(ev->label, s->label, sizeof(ev->label));
	ev->endpoint = s->endpoint;
	ev->pid = s->pid;
	ev->restarts = s->restarts;
	ev->times = times;
}

/*
 * Look at /proc/service once: what has happened since the last look, at
 * most max events into ev.  The number of them, or -1 if there is no
 * /proc/service to look at.
 */
int
nt_look(struct nt_event *ev, int max)
{
	char path[PATH_MAX], buf[512];
	struct dirent *de;
	struct svc *s;
	int ep, pid, restarts, i, n = 0;
	DIR *d;

	if ((d = opendir("/proc/service")) == NULL)
		return -1;
	looks++;
	while ((de = readdir(d)) != NULL) {
		if (de->d_name[0] == '.')
			continue;
		snprintf(path, sizeof(path), "/proc/service/%s", de->d_name);
		if (read_file(path, buf, sizeof(buf)) <= 0)
			continue;
		ep = field(buf, "endpoint:", 0);
		pid = field(buf, "pid:", 0);
		restarts = field(buf, "restarts:", 0);
		for (i = 0; i < nsvcs; i++)
			if (strcmp(svcs[i].label, de->d_name) == 0)
				break;
		if (i == nsvcs) {
			/* Not known: there from the start, or started since. */
			if (nsvcs == MAXSVC)
				continue;
			s = &svcs[nsvcs++];
			strlcpy(s->label, de->d_name, sizeof(s->label));
			s->endpoint = ep;
			s->pid = pid;
			s->restarts = restarts;
			s->seen = looks;
			s->gone = false;
			continue;
		}
		s = &svcs[i];
		s->seen = looks;
		if (s->gone) {
			s->gone = false;
			s->endpoint = ep;
			s->pid = pid;
			s->restarts = restarts;
			if (n < max)
				event(&ev[n++], NT_BACK, s, 1);
			continue;
		}
		if (s->endpoint != ep || s->restarts != restarts) {	/* restart */
			i = restarts - s->restarts;
			s->endpoint = ep;
			s->pid = pid;
			s->restarts = restarts;
			if (n < max)
				event(&ev[n++], NT_RESTART, s, i > 1 ? i : 1);
		}
	}
	closedir(d);

	for (i = 0; i < nsvcs; i++) {
		s = &svcs[i];
		if (!s->gone && looks - s->seen >= GONE_LOOKS) {
			s->gone = true;
			if (n < max)
				event(&ev[n++], NT_STOPPED, s, 1);
		}
	}
	return n;
}

/* How many services the last look saw. */
int
nt_services(void)
{
	int i, n = 0;

	for (i = 0; i < nsvcs; i++)
		if (svcs[i].seen == looks)
			n++;
	return n;
}
