/*
 * The x-ray's model: one sample of the system, as a graph.
 *
 * Every process that may be drawn has its row of the kernel's message
 * matrix read: the windows' clients and the compositor, every system
 * process, and the kernel's tasks.  An edge is a pair of processes with
 * messages between them; its count is the sender's own (a row says what
 * its process sent), and its rate is how much that count grew since the
 * last sample, a second, smoothed.  Which nodes are shown is decided on
 * the rates: a service is shown if it is at most XR_MAXDEPTH active edges
 * from a window or the compositor, the kernel if anything shown talks to
 * it.  A user process that is not a window is read only if a window's
 * row names it - which on MINIX it cannot, since processes do not send to
 * each other - and is never shown.
 *
 * Counts are per tenant: an edge is keyed by both endpoints, not slots, so
 * a restarted service (a new endpoint) starts its edges afresh instead of
 * inheriting a dead one's.  The matrix itself does the same in the kernel:
 * what was sent to a process that has gone is in the senders' "gone"
 * column, not under the slot's next tenant.
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <minix/type.h>		/* endpoint_t */
#include <minix/com.h>		/* KERNEL, NR_TASKS */
#include <minix/endpoint.h>	/* NONE */

#include "xray.h"

#define RATE_TAU	3.0	/* seconds: how fast a rate follows a change */
#define FLASH_MS	2500	/* how long a restart shows */
#define ACTIVE		0.05	/* messages a second that make an edge live */
#define SVC_EVERY	1	/* samples between reads of /proc/service */

/*
 * What was counted last time, by pair of tenants: what a rate is made of.
 * Open addressing on the pair; a slot not seen for a few samples is free.
 */
#define NPREV	4096
struct prev {
	int32_t from, to;
	uint64_t count;
	double rate;
	unsigned seen;		/* the sample it was last found in; 0: free */
};
static struct prev prev[NPREV];

/* A process seen in /proc: psinfo is read once per tenant. */
struct proc {
	pid_t pid;
	int32_t ep;
	char type;
	char name[16];
	uid_t uid;
	unsigned long mem;
	unsigned seen;
};
static struct proc procs[512];
static int nprocs;

/* The services, from /proc/service: a label, and its restarts. */
struct svc {
	char label[32];
	int32_t ep;
	int restarts;
	unsigned flash_ms;	/* to give the node it has now */
};
static struct svc svcs[128];
static int nsvcs;

/*===========================================================================*
 *    the compositor's socket                                                *
 *===========================================================================*/
const char *
xr_sock_path(void)
{
	static char path[sizeof(((struct sockaddr_un *)0)->sun_path)];
	const char *p = getenv("MEECHOWM_SOCK"), *dir;

	if (p != NULL)
		return p;
	if ((dir = getenv("XDG_RUNTIME_DIR")) == NULL)
		return NULL;
	if ((size_t)snprintf(path, sizeof(path), "%s/meechowm.sock", dir) >=
	    sizeof(path))
		return NULL;
	return path;
}

/*
 * A command to the compositor, as meechowm -m sends it; its reply in
 * reply.  The peer's pid comes back too, from the socket's credentials -
 * the same way the compositor learns its clients' - and is the pid of the
 * compositor process for the model.  Returns the number of bytes of reply,
 * or -1.
 */
static int
command(const char *line, char *reply, size_t size, pid_t *peer)
{
	struct sockaddr_un sa;
	struct unpcbid cred;
	socklen_t len;
	const char *path = xr_sock_path();
	size_t n = 0;
	ssize_t r;
	int fd;

	if (path == NULL || size == 0)
		return -1;
	memset(&sa, 0, sizeof(sa));
	sa.sun_family = AF_UNIX;
	strlcpy(sa.sun_path, path, sizeof(sa.sun_path));
	if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
		return -1;
	if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		close(fd);
		return -1;
	}
	if (peer != NULL) {
		len = sizeof(cred);
		*peer = getsockopt(fd, 0, LOCAL_PEEREID, &cred, &len) == 0 ?
		    cred.unp_pid : 0;
	}
	if (write(fd, line, strlen(line)) < 0 || write(fd, "\n", 1) < 0) {
		close(fd);
		return -1;
	}
	while (n + 1 < size && (r = read(fd, reply + n, size - 1 - n)) > 0)
		n += (size_t)r;
	reply[n] = '\0';
	close(fd);
	return (int)n;
}

int
xr_command(const char *line, char *reply, size_t size)
{
	return command(line, reply, size, NULL);
}

/*
 * The windows on the screen, from the compositor's "state": the active
 * workspace's columns, at where they are drawn now ("at", which differs
 * from x only while one moves), and those that show at all.
 */
static bool
read_state(struct xr_model *m)
{
	static char buf[16384];
	char *line, *save, title[64];
	int ws = 0, active = -1, wi, ci, x, y, w, h, at, pid, n;
	char focus[8], width[8];

	if (command("state", buf, sizeof(buf), &m->desk_pid) <= 0)
		return false;
	m->nwin = 0;
	for (line = strtok_r(buf, "\n", &save); line != NULL;
	    line = strtok_r(NULL, "\n", &save)) {
		if (sscanf(line, "screen %dx%d area %d,%d %dx%d", &m->sw,
		    &m->sh, &m->ax, &m->ay, &m->aw, &m->ah) == 6)
			continue;
		if (strncmp(line, "workspace ", 10) == 0) {
			ws++;
			if (strstr(line, " active ") != NULL)
				active = ws;
			continue;
		}
		title[0] = '\0';
		if (sscanf(line, "column %d.%d %d %d %d %d width %7s %7s at %d "
		    "size %*dx%*d pid %d title %n", &wi, &ci, &x, &y, &w, &h,
		    width, focus, &at, &pid, &n) != 10)
			continue;
		strlcpy(title, line + n, sizeof(title));
		if (wi != active || at + w <= 0 || at >= m->sw || pid <= 0 ||
		    m->nwin >= XR_MAXWIN)
			continue;
		m->win[m->nwin].pid = pid;
		m->win[m->nwin].x = at;
		m->win[m->nwin].y = y;
		m->win[m->nwin].w = w;
		m->win[m->nwin].h = h;
		m->win[m->nwin].focus = strcmp(focus, "focus") == 0;
		strlcpy(m->win[m->nwin].title, title,
		    sizeof(m->win[m->nwin].title));
		m->win[m->nwin].node = -1;
		m->nwin++;
	}
	return m->sw > 0;
}

/*===========================================================================*
 *    /proc                                                                  *
 *===========================================================================*/
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

/* psinfo: "version type endpoint name state ... memory nice uid". */
static struct proc *
proc_info(pid_t pid, unsigned sample)
{
	char path[64], buf[512], name[16];
	char type, state;
	struct proc *p = NULL;
	unsigned long mem;
	long t[9];
	int ver, ep, i;
	unsigned uid;

	for (i = 0; i < nprocs; i++)
		if (procs[i].pid == pid) {
			p = &procs[i];
			break;
		}
	/*
	 * Seen in the last sample: the same tenant (PM does not hand a pid
	 * out again that soon), and its memory is read again only now and
	 * then - psinfo is a round to MIB, which copies whole process tables.
	 */
	if (p != NULL && p->seen + 1 >= sample && sample % 8 != 0) {
		p->seen = sample;
		return p;
	}
	snprintf(path, sizeof(path), "/proc/%d/psinfo", (int)pid);
	if (read_file(path, buf, sizeof(buf)) <= 0 ||
	    sscanf(buf, "%d %c %d %15s %c %ld %ld %ld %ld %ld %ld %ld %lu "
	    "%ld %u", &ver, &type, &ep, name, &state, &t[0], &t[1], &t[2],
	    &t[3], &t[4], &t[5], &t[6], &mem, &t[7], &uid) != 15)
		return NULL;
	if (p == NULL) {
		/* Forget whoever has not been seen for a while. */
		if (nprocs == (int)(sizeof(procs) / sizeof(procs[0]))) {
			for (i = 0; i < nprocs; i++)
				if (procs[i].seen + 2 < sample)
					break;
			if (i == nprocs)
				return NULL;
			p = &procs[i];
		} else
			p = &procs[nprocs++];
	}
	p->pid = pid;
	p->ep = ep;
	p->type = type;
	strlcpy(p->name, name, sizeof(p->name));
	p->uid = uid;
	p->mem = mem;
	p->seen = sample;
	return p;
}

static int
node_by_ep(const struct xr_model *m, int32_t ep)
{
	int i;

	for (i = 0; i < m->nnode; i++)
		if (m->node[i].ep == ep)
			return i;
	return -1;
}

static int
add_node(struct xr_model *m, enum xr_kind kind, const struct proc *p)
{
	struct xr_node *n;

	if (m->nnode >= XR_MAXNODE)
		return -1;
	n = &m->node[m->nnode];
	memset(n, 0, sizeof(*n));
	n->kind = kind;
	n->depth = -1;
	if (kind == XR_KERNEL) {
		n->ep = KERNEL;
		n->type = 'T';
		strlcpy(n->name, "kernel", sizeof(n->name));
		strlcpy(n->label, "kernel", sizeof(n->label));
	} else {
		n->ep = p->ep;
		n->pid = p->pid;
		n->type = p->type;
		n->uid = p->uid;
		n->mem = p->mem;
		strlcpy(n->name, p->name, sizeof(n->name));
		strlcpy(n->label, p->name, sizeof(n->label));
	}
	return m->nnode++;
}

static void
add_count(struct xr_model *m, int from, int to, uint64_t count)
{
	int i;

	if (from < 0 || to < 0 || from == to || count == 0)
		return;
	for (i = 0; i < m->nedge; i++)
		if (m->edge[i].from == from && m->edge[i].to == to) {
			m->edge[i].count += count;
			return;
		}
	if (m->nedge >= XR_MAXEDGE)
		return;
	m->edge[m->nedge].from = from;
	m->edge[m->nedge].to = to;
	m->edge[m->nedge].count = count;
	m->edge[m->nedge].rate = 0;
	m->edge[m->nedge].shown = false;
	m->nedge++;
}

/*
 * A row of /proc/<pid>/ipc into edges from node n.  Receivers are matched
 * by endpoint against nodes already made; kernel tasks are one node.
 */
static void
read_row(struct xr_model *m, int n)
{
	static char buf[16384];
	char path[64], *line, *save, name[16];
	unsigned long long c;
	int slot, ep, pid, to;
	int kern = -1;

	snprintf(path, sizeof(path), "/proc/%d/ipc", (int)m->node[n].pid);
	if (read_file(path, buf, sizeof(buf)) <= 0)
		return;
	for (line = strtok_r(buf, "\n", &save); line != NULL;
	    line = strtok_r(NULL, "\n", &save)) {
		if (sscanf(line, "self %d %d", &slot, &ep) == 2) {
			m->node[n].slot = slot;
			continue;
		}
		if (sscanf(line, "kernel %llu", &c) == 1) {
			m->node[n].kernel += c;
			continue;
		}
		if (sscanf(line, "to %d %d %d %15s %llu", &slot, &ep, &pid,
		    name, &c) != 5)
			continue;
		m->node[n].sent += c;
		if (slot < NR_TASKS) {
			if (kern < 0 && (kern = node_by_ep(m, KERNEL)) < 0)
				kern = add_node(m, XR_KERNEL, NULL);
			to = kern;
		} else
			to = node_by_ep(m, ep);
		add_count(m, n, to, c);
	}
}

/* The kernel's tasks' rows, into the one kernel node. */
static void
read_kernel_rows(struct xr_model *m)
{
	static char buf[16384];
	char path[64], *line, *save, name[16];
	unsigned long long c;
	int slot, ep, pid, k, task;

	if ((k = node_by_ep(m, KERNEL)) < 0 &&
	    (k = add_node(m, XR_KERNEL, NULL)) < 0)
		return;
	for (task = -1; task >= -NR_TASKS; task--) {
		snprintf(path, sizeof(path), "/proc/%d/ipc", task);
		if (read_file(path, buf, sizeof(buf)) <= 0)
			continue;
		for (line = strtok_r(buf, "\n", &save); line != NULL;
		    line = strtok_r(NULL, "\n", &save)) {
			if (sscanf(line, "to %d %d %d %15s %llu", &slot, &ep,
			    &pid, name, &c) != 5 || slot < NR_TASKS)
				continue;
			m->node[k].sent += c;
			add_count(m, k, node_by_ep(m, ep), c);
		}
	}
}

/*
 * /proc/service: the label of each service, and whether it has been
 * restarted since the last look - its "restarts" grown, or its endpoint
 * changed (RS gives a restarted service a new one).
 */
static void
read_services(void)
{
	char path[PATH_MAX], buf[512], *p;
	struct dirent *de;
	struct svc *s;
	int ep, restarts, i;
	DIR *d;

	if ((d = opendir("/proc/service")) == NULL)
		return;
	while ((de = readdir(d)) != NULL) {
		if (de->d_name[0] == '.')
			continue;
		snprintf(path, sizeof(path), "/proc/service/%s", de->d_name);
		if (read_file(path, buf, sizeof(buf)) <= 0)
			continue;
		ep = NONE;
		restarts = 0;
		if ((p = strstr(buf, "endpoint:")) != NULL)
			ep = atoi(p + 9);
		if ((p = strstr(buf, "restarts:")) != NULL)
			restarts = atoi(p + 9);
		for (i = 0; i < nsvcs; i++)
			if (strcmp(svcs[i].label, de->d_name) == 0)
				break;
		if (i == nsvcs) {
			if (nsvcs == (int)(sizeof(svcs) / sizeof(svcs[0])))
				continue;
			s = &svcs[nsvcs++];
			strlcpy(s->label, de->d_name, sizeof(s->label));
			s->ep = ep;
			s->restarts = restarts;
			s->flash_ms = 0;
			continue;
		}
		s = &svcs[i];
		if (ep != s->ep || restarts != s->restarts)
			s->flash_ms = FLASH_MS;
		s->ep = ep;
		s->restarts = restarts;
	}
	closedir(d);
}

/*===========================================================================*
 *    rates                                                                  *
 *===========================================================================*/
static struct prev *
prev_find(int32_t from, int32_t to, unsigned sample)
{
	unsigned h = ((unsigned)from * 2654435761u ^ (unsigned)to * 40503u) %
	    NPREV, i;
	struct prev *p, *free_p = NULL;

	for (i = 0; i < NPREV; i++) {
		p = &prev[(h + i) % NPREV];
		if (p->seen != 0 && p->from == from && p->to == to)
			return p;
		if (free_p == NULL && (p->seen == 0 || p->seen + 5 < sample))
			free_p = p;
		if (p->seen == 0)
			break;		/* the pair would have been before it */
	}
	return free_p;
}

static void
rates(struct xr_model *m, unsigned elapsed_ms)
{
	struct xr_edge *e;
	struct prev *p;
	double dt = elapsed_ms / 1000.0, now, k;
	int i;

	k = dt > 0 ? 1 - exp(-dt / RATE_TAU) : 0;
	for (i = 0; i < m->nedge; i++) {
		e = &m->edge[i];
		if ((p = prev_find(m->node[e->from].ep, m->node[e->to].ep,
		    m->samples)) == NULL)
			continue;
		if (p->seen == 0 || p->from != m->node[e->from].ep ||
		    p->to != m->node[e->to].ep) {
			p->from = m->node[e->from].ep;
			p->to = m->node[e->to].ep;
			/* New: its first count is history, not a burst. */
			p->count = e->count;
			p->rate = 0;
		}
		/*
		 * A count that went down is a tenant that changed under the
		 * same endpoints - kernel tasks folded into one node lose a
		 * receiver to "gone" - and is taken as it is.
		 */
		now = e->count >= p->count && dt > 0 ?
		    (double)(e->count - p->count) / dt : 0;
		p->rate += (now - p->rate) * k;
		if (p->rate < 0.001)
			p->rate = 0;
		p->count = e->count;
		p->seen = m->samples;
		e->rate = p->rate;
	}
}

/*
 * Which nodes and edges show: the windows and the compositor, and every
 * service within XR_MAXDEPTH live edges of them, whichever way the
 * messages go.  The kernel shows if a shown node has a live edge to it.
 */
static void
choose(struct xr_model *m)
{
	struct xr_edge *e;
	bool grew = true;
	int i, d, a, b;

	for (i = 0; i < m->nnode; i++) {
		m->node[i].depth = m->node[i].kind == XR_WINDOW ||
		    m->node[i].kind == XR_DESK ? 0 : -1;
		m->node[i].rate = 0;
	}
	for (d = 0; d < XR_MAXDEPTH && grew; d++) {
		grew = false;
		for (i = 0; i < m->nedge; i++) {
			e = &m->edge[i];
			if (e->rate < ACTIVE)
				continue;
			a = e->from;
			b = e->to;
			if (m->node[a].depth != d) {
				a = e->to;
				b = e->from;
			}
			if (m->node[a].depth != d || m->node[b].depth >= 0 ||
			    m->node[b].kind != XR_SERVICE)
				continue;
			m->node[b].depth = d + 1;
			grew = true;
		}
	}
	for (i = 0; i < m->nedge; i++) {
		e = &m->edge[i];
		if (e->rate < ACTIVE)
			continue;
		if (m->node[e->from].kind == XR_KERNEL &&
		    m->node[e->to].depth >= 0)
			m->node[e->from].depth = XR_MAXDEPTH + 1;
		if (m->node[e->to].kind == XR_KERNEL &&
		    m->node[e->from].depth >= 0)
			m->node[e->to].depth = XR_MAXDEPTH + 1;
	}
	m->rate = 0;
	for (i = 0; i < m->nnode; i++)
		m->node[i].shown = m->node[i].depth >= 0 &&
		    m->node[i].kind != XR_OTHER;
	for (i = 0; i < m->nedge; i++) {
		e = &m->edge[i];
		e->shown = e->rate >= ACTIVE && m->node[e->from].shown &&
		    m->node[e->to].shown;
		if (!e->shown)
			continue;
		m->node[e->from].rate += e->rate;
		m->node[e->to].rate += e->rate;
		m->rate += e->rate;
	}
}

/*===========================================================================*
 *    a sample                                                               *
 *===========================================================================*/
/*
 * Read the system again.  elapsed_ms is the time since the last sample,
 * for the rates; the first sample has none and shows no lines yet.
 */
bool
xr_sample(struct xr_model *m, unsigned elapsed_ms)
{
	struct proc *p;
	struct dirent *de;
	char *end;
	long pid;
	int i, j, n;
	DIR *d;

	m->samples++;
	if (!read_state(m))
		return false;
	if ((m->samples - 1) % SVC_EVERY == 0)
		read_services();

	/* The nodes, from /proc. */
	m->nnode = 0;
	m->nedge = 0;
	if ((d = opendir("/proc")) == NULL)
		return false;
	while ((de = readdir(d)) != NULL) {
		pid = strtol(de->d_name, &end, 10);
		if (*end != '\0' || end == de->d_name || pid < 0)
			continue;	/* not a pid; tasks are one node */
		if ((p = proc_info((pid_t)pid, m->samples)) == NULL)
			continue;
		if (p->pid == m->desk_pid)
			n = add_node(m, XR_DESK, p);
		else if (p->type == 'S')
			n = add_node(m, XR_SERVICE, p);
		else {
			for (i = 0; i < m->nwin; i++)
				if (m->win[i].pid == p->pid)
					break;
			n = add_node(m, i < m->nwin ? XR_WINDOW : XR_OTHER, p);
		}
		(void)n;
	}
	closedir(d);
	for (i = 0; i < m->nwin; i++)
		m->win[i].node = -1;
	for (i = 0; i < m->nnode; i++) {
		for (j = 0; j < nsvcs; j++)
			if (svcs[j].ep == m->node[i].ep) {
				strlcpy(m->node[i].label, svcs[j].label,
				    sizeof(m->node[i].label));
				m->node[i].flash_ms = svcs[j].flash_ms;
				break;
			}
		for (j = 0; j < m->nwin; j++)
			if (m->win[j].pid == m->node[i].pid &&
			    m->node[i].kind == XR_WINDOW)
				m->win[j].node = i;
	}

	/* The edges, from the rows. */
	for (i = 0; i < m->nnode; i++)
		if (m->node[i].kind != XR_OTHER && m->node[i].kind != XR_KERNEL)
			read_row(m, i);
	read_kernel_rows(m);
	rates(m, elapsed_ms);
	choose(m);
	return true;
}

/* Time passes between samples: the flashes fade. */
void
xr_tick(struct xr_model *m, unsigned elapsed_ms)
{
	int i;

	for (i = 0; i < m->nnode; i++)
		m->node[i].flash_ms = m->node[i].flash_ms > elapsed_ms ?
		    m->node[i].flash_ms - elapsed_ms : 0;
	for (i = 0; i < nsvcs; i++)
		svcs[i].flash_ms = svcs[i].flash_ms > elapsed_ms ?
		    svcs[i].flash_ms - elapsed_ms : 0;
}

/* For -d: the services as the last look at /proc/service saw them. */
void
xr_print_services(FILE *f)
{
	int i;

	for (i = 0; i < nsvcs; i++)
		fprintf(f, "service %s %d %d %u\n", svcs[i].label, (int)svcs[i].ep,
		    svcs[i].restarts, svcs[i].flash_ms);
}

bool
xr_flashing(const struct xr_model *m)
{
	int i;

	for (i = 0; i < m->nnode; i++)
		if (m->node[i].shown && m->node[i].flash_ms > 0)
			return true;
	return false;
}
