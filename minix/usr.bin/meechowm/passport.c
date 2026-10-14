/*
 * The passport (stage 11.7): what the system knows of a window's process,
 * drawn by the compositor in the window's frame - its program, pid and
 * user, how much memory it has, and which processes it sends messages to,
 * how many a second.  The x-ray view asks for it with a click on a window
 * (meechowm -m passport PID), Super+i for the focused one; the same again
 * puts it away.
 *
 * Why the compositor and not a client: a window's own pixels are the
 * program's to draw, and a program can draw anything in them - a password
 * dialog of another program's, say.  The passport lies over the window,
 * in the compositor's own colour, and says only what came from the system
 * rather than from the program:
 *
 *   the pid       from the credentials of the client's socket (uds,
 *                 LOCAL_PEEREID) - wl_client_get_credentials()
 *   the program   /proc/<pid>/psinfo: the name exec gave the process
 *   the user      its effective uid, from the same line, by /etc/passwd
 *   the memory    VM's count, the same line
 *   whom it talks to   its row of the kernel's message matrix,
 *                 /proc/<pid>/ipc (<minix/ipcflow.h>)
 *
 * and nothing the program said of itself - its title and its arguments
 * are its own words.
 *
 * Drawn in Spleen 8x16, the screen console's font (drivers/video/fb), so
 * that the compositor carries no font machinery: the passport is a few
 * lines, and the bitmap is two kilobytes.
 */
#include <sys/types.h>

#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <drm_fourcc.h>
#include <pixman.h>
#include <wayland-server-core.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>

#include "style.h"
#include "wm.h"

/* What NetBSD's <dev/wscons/wsconsio.h> says of a font, as far as needed. */
#define WSDISPLAY_FONTENC_ISO	0
#define WSDISPLAY_FONTORDER_L2R	1
struct wsdisplay_font {
	const char *name;
	int firstchar, numchars, encoding;
	u_int fontwidth, fontheight, stride;
	int bitorder, byteorder;
	void *data;
};
#include <dev/wsfont/spleen8x16.h>

#define CW		8	/* the font's cell */
#define CH		16
#define PAD		6
#define BORDER		2
#define LINES		3
#define REFRESH_MS	1000
#define MAXDST		64

/* The one passport: whose, and what was counted for its rates. */
static struct {
	struct tinywl_server *server;
	pid_t pid;			/* 0: none */
	struct wl_event_source *timer;
	struct wl_list shown;		/* struct pp_node.link */
	struct { int32_t ep; char name[16]; uint64_t count; double rate; }
	    dst[MAXDST];
	int ndst;
	uint64_t tsc, freq;		/* the row's, for the rates */
	bool counted;
	char line[LINES][160];
} pp;

/* A passport drawn on one window. */
struct pp_node {
	struct wl_list link;
	struct tinywl_toplevel *toplevel;
	struct wlr_scene_buffer *buffer;
	struct wl_listener destroy;
};

/*===========================================================================*
 *    a buffer of our own pixels                                             *
 *===========================================================================*/
struct pp_buffer {
	struct wlr_buffer base;
	uint32_t *px;
	size_t stride;
};

static void
pb_destroy(struct wlr_buffer *b)
{
	struct pp_buffer *p = wl_container_of(b, p, base);

	wlr_buffer_finish(b);
	free(p->px);
	free(p);
}

static bool
pb_begin(struct wlr_buffer *b, uint32_t flags, void **data, uint32_t *format,
	size_t *stride)
{
	struct pp_buffer *p = wl_container_of(b, p, base);

	if (flags & WLR_BUFFER_DATA_PTR_ACCESS_WRITE)
		return false;
	*data = p->px;
	*format = DRM_FORMAT_ARGB8888;
	*stride = p->stride;
	return true;
}

static void
pb_end(struct wlr_buffer *b)
{
}

static const struct wlr_buffer_impl pb_impl = {
	.destroy = pb_destroy,
	.begin_data_ptr_access = pb_begin,
	.end_data_ptr_access = pb_end,
};

/*===========================================================================*
 *    what the system says                                                   *
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

static int
by_rate(const void *a, const void *b)
{
	const typeof(pp.dst[0]) *x = a, *y = b;

	if (x->rate != y->rate)
		return x->rate < y->rate ? 1 : -1;
	return x->count < y->count ? 1 : x->count > y->count ? -1 : 0;
}

/*
 * The three lines, read afresh.  The rates are the counts' growth since
 * the last read; the first read has counts only.
 */
static bool
gather(void)
{
	static char buf[16384];
	typeof(pp.dst) dst;
	char path[64], name[16], type, state, *line, *save, *p;
	unsigned long long c, tsc = 0, freq = 0;
	unsigned long mem;
	unsigned uid;
	long t[9];
	double dt, total = 0;
	int ver, ep, slot, pid, i, j, n = 0, len;
	struct passwd *pw;
	char user[32];

	snprintf(path, sizeof(path), "/proc/%d/psinfo", (int)pp.pid);
	if (read_file(path, buf, sizeof(buf)) <= 0 ||
	    sscanf(buf, "%d %c %d %15s %c %ld %ld %ld %ld %ld %ld %ld %lu "
	    "%ld %u", &ver, &type, &ep, name, &state, &t[0], &t[1], &t[2],
	    &t[3], &t[4], &t[5], &t[6], &mem, &t[7], &uid) != 15)
		return false;
	if ((pw = getpwuid((uid_t)uid)) != NULL)
		snprintf(user, sizeof(user), "%s", pw->pw_name);
	else
		snprintf(user, sizeof(user), "uid %u", uid);
	snprintf(pp.line[0], sizeof(pp.line[0]), "%s  pid %d  %s", name,
	    (int)pp.pid, user);

	snprintf(path, sizeof(path), "/proc/%d/ipc", (int)pp.pid);
	if (read_file(path, buf, sizeof(buf)) <= 0)
		return false;
	for (line = strtok_r(buf, "\n", &save); line != NULL;
	    line = strtok_r(NULL, "\n", &save)) {
		if (sscanf(line, "tsc %llu", &tsc) == 1 ||
		    sscanf(line, "freq %llu", &freq) == 1)
			continue;
		if (sscanf(line, "to %d %d %d %15s %llu", &slot, &ep, &pid,
		    name, &c) != 5 || n == MAXDST)
			continue;
		dst[n].ep = ep;
		strlcpy(dst[n].name, name, sizeof(dst[n].name));
		dst[n].count = c;
		dst[n].rate = 0;
		/* The rate: against the same receiver's count last time. */
		dt = pp.counted && freq > 0 && tsc > pp.tsc ?
		    (double)(tsc - pp.tsc) / (double)freq : 0;
		for (j = 0; dt > 0 && j < pp.ndst; j++)
			if (pp.dst[j].ep == ep && c >= pp.dst[j].count) {
				dst[n].rate = (double)(c - pp.dst[j].count) /
				    dt;
				break;
			}
		total += dst[n].rate;
		n++;
	}
	memcpy(pp.dst, dst, sizeof(dst));
	pp.ndst = n;
	pp.tsc = tsc;
	pp.freq = freq;

	if (pp.counted)
		snprintf(pp.line[1], sizeof(pp.line[1]), "memory %.1f MiB  "
		    "sends %.0f messages/s", mem / 1048576.0, total);
	else
		snprintf(pp.line[1], sizeof(pp.line[1]), "memory %.1f MiB",
		    mem / 1048576.0);
	pp.counted = true;

	qsort(dst, (size_t)n, sizeof(dst[0]), by_rate);
	p = pp.line[2];
	len = snprintf(p, sizeof(pp.line[2]), "to");
	for (i = 0; i < n && i < 6 && len < (int)sizeof(pp.line[2]); i++) {
		if (dst[i].rate > 0)
			len += snprintf(p + len, sizeof(pp.line[2]) - len,
			    "  %s %.0f/s", dst[i].name, dst[i].rate);
		else
			len += snprintf(p + len, sizeof(pp.line[2]) - len,
			    "  %s %llu", dst[i].name,
			    (unsigned long long)dst[i].count);
	}
	return true;
}

/*===========================================================================*
 *    drawing                                                                *
 *===========================================================================*/
static void
glyphs(uint32_t *px, size_t stride, int x, int y, const char *s,
	uint32_t argb, int maxw)
{
	const uint8_t *g;
	unsigned c;
	int r, b;

	for (; *s != '\0' && x + CW <= maxw; s++, x += CW) {
		c = (unsigned char)*s;
		if (c < (unsigned)spleen8x16.firstchar || c >=
		    (unsigned)(spleen8x16.firstchar + spleen8x16.numchars))
			c = '?';
		g = (const uint8_t *)spleen8x16.data +
		    (c - spleen8x16.firstchar) * CH;
		for (r = 0; r < CH; r++)
			for (b = 0; b < CW; b++)
				if (g[r] & (0x80 >> b))
					*(uint32_t *)((char *)px + (y + r) *
					    stride + (x + b) * 4) = argb;
	}
}

/* The passport as a buffer, no wider than maxw; NULL if it cannot be. */
static struct pp_buffer *
render(int maxw)
{
	struct pp_buffer *b;
	size_t stride;
	int w = 0, h, i, x, y, n;
	uint32_t bg, edge = 0xff000000u | STYLE_ACCENT;

	for (i = 0; i < LINES; i++) {
		n = (int)strlen(pp.line[i]) * CW;
		if (n > w)
			w = n;
	}
	w += 2 * (PAD + BORDER);
	if (w > maxw)
		w = maxw;
	h = LINES * CH + 2 * (PAD + BORDER);
	if (w < 4 * CW + 2 * (PAD + BORDER))
		return NULL;
	stride = (size_t)w * 4;
	if ((b = calloc(1, sizeof(*b))) == NULL ||
	    (b->px = malloc(stride * h)) == NULL) {
		free(b);
		return NULL;
	}
	b->stride = stride;
	/* Premultiplied: the fill's 0xf0 of alpha, its colour scaled too. */
	bg = 0xf0000000u |
	    (((STYLE_SURFACE >> 16 & 0xff) * 0xf0 / 0xff) << 16) |
	    (((STYLE_SURFACE >> 8 & 0xff) * 0xf0 / 0xff) << 8) |
	    ((STYLE_SURFACE & 0xff) * 0xf0 / 0xff);
	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++)
			b->px[y * w + x] = x < BORDER || y < BORDER ||
			    x >= w - BORDER || y >= h - BORDER ? edge : bg;
	glyphs(b->px, stride, BORDER + PAD, BORDER + PAD, pp.line[0],
	    0xff000000u | STYLE_ACCENT, w - BORDER - PAD);
	glyphs(b->px, stride, BORDER + PAD, BORDER + PAD + CH, pp.line[1],
	    0xff000000u | STYLE_TEXT, w - BORDER - PAD);
	glyphs(b->px, stride, BORDER + PAD, BORDER + PAD + 2 * CH, pp.line[2],
	    0xff000000u | STYLE_MUTED, w - BORDER - PAD);
	wlr_buffer_init(&b->base, &pb_impl, w, h);
	return b;
}

/*===========================================================================*
 *    on the windows                                                         *
 *===========================================================================*/
static pid_t
pid_of(struct tinywl_toplevel *t)
{
	pid_t pid = 0;
	uid_t uid;
	gid_t gid;

	wl_client_get_credentials(t->xdg_toplevel->base->client->client, &pid,
	    &uid, &gid);
	return pid;
}

static void
node_destroyed(struct wl_listener *listener, void *data)
{
	struct pp_node *n = wl_container_of(listener, n, destroy);

	/* With its window's tree: the window has gone. */
	wl_list_remove(&n->destroy.link);
	wl_list_remove(&n->link);
	free(n);
}

static void
take_down(void)
{
	struct pp_node *n, *tmp;

	wl_list_for_each_safe(n, tmp, &pp.shown, link)
		wlr_scene_node_destroy(&n->buffer->node);	/* -> above */
}

/* Draw it on every window of the process, or put it away if there is none. */
static void
put_up(void)
{
	struct tinywl_server *s = pp.server;
	struct mw_workspace *ws;
	struct tinywl_toplevel *t;
	struct pp_node *n;
	struct pp_buffer *b;
	struct wlr_box *geo;
	int windows = 0;

	if (!gather()) {
		passport_off();
		return;
	}
	wl_list_for_each(ws, &s->workspaces, link)
		wl_list_for_each(t, &ws->columns, column_link) {
			if (pid_of(t) != pp.pid)
				continue;
			windows++;
			geo = &t->xdg_toplevel->base->geometry;
			if ((b = render(t->box.width - 2 * PAD)) == NULL)
				continue;
			wl_list_for_each(n, &pp.shown, link)
				if (n->toplevel == t)
					break;
			if (&n->link == &pp.shown) {
				if ((n = calloc(1, sizeof(*n))) == NULL) {
					wlr_buffer_drop(&b->base);
					continue;
				}
				n->toplevel = t;
				n->buffer = wlr_scene_buffer_create(t->scene_tree,
				    &b->base);
				if (n->buffer == NULL) {
					free(n);
					wlr_buffer_drop(&b->base);
					continue;
				}
				n->destroy.notify = node_destroyed;
				wl_signal_add(&n->buffer->node.events.destroy,
				    &n->destroy);
				wl_list_insert(&pp.shown, &n->link);
			} else
				wlr_scene_buffer_set_buffer(n->buffer, &b->base);
			wlr_buffer_drop(&b->base);
			/* Over the window's corner: the last child, the top. */
			wlr_scene_node_raise_to_top(&n->buffer->node);
			wlr_scene_node_set_position(&n->buffer->node,
			    geo->x + PAD, geo->y + PAD);
		}
	if (windows == 0)
		passport_off();
}

static int
refresh(void *data)
{
	if (pp.pid == 0)
		return 0;
	put_up();
	if (pp.pid != 0)
		wl_event_source_timer_update(pp.timer, REFRESH_MS);
	return 0;
}

void
passport_off(void)
{
	pp.pid = 0;
	pp.counted = false;
	pp.ndst = 0;
	if (pp.server != NULL)
		take_down();
	if (pp.timer != NULL)
		wl_event_source_timer_update(pp.timer, 0);
}

static void
passport_on(struct tinywl_server *s, pid_t pid)
{
	if (pp.server == NULL) {
		pp.server = s;
		wl_list_init(&pp.shown);
	}
	if (pp.timer == NULL)
		pp.timer = wl_event_loop_add_timer(
		    wl_display_get_event_loop(s->wl_display), refresh, NULL);
	if (pp.pid != pid) {
		take_down();
		pp.counted = false;
		pp.ndst = 0;
	}
	pp.pid = pid;
	put_up();
	if (pp.pid != 0 && pp.timer != NULL)
		wl_event_source_timer_update(pp.timer, REFRESH_MS);
}

/*
 * "passport" - the focused window's, or away if it is up; "passport PID" -
 * that process's, or away if it is up; "passport off".  The reply is what
 * the passport says.
 */
void
passport_command(struct tinywl_server *s, const char *arg,
	struct mw_reply *r)
{
	char *end;
	long pid;
	int i;

	if (strcmp(arg, "off") == 0) {
		passport_off();
		wm_reply(r, "passport off\n");
		return;
	}
	if (arg[0] == '\0') {
		if (s->ws->focus == NULL) {
			r->error = true;
			wm_reply(r, "error: passport: no window\n");
			return;
		}
		pid = pid_of(s->ws->focus);
	} else {
		pid = strtol(arg, &end, 10);
		if (*end != '\0' || pid <= 0) {
			r->error = true;
			wm_reply(r, "error: passport %s: not a pid\n", arg);
			return;
		}
	}
	if (pid == pp.pid) {
		passport_off();
		wm_reply(r, "passport off\n");
		return;
	}
	passport_on(s, (pid_t)pid);
	if (pp.pid == 0) {
		r->error = true;
		wm_reply(r, "error: passport %ld: no window of it\n", pid);
		return;
	}
	wm_reply(r, "passport %d\n", (int)pp.pid);
	for (i = 0; i < LINES; i++)
		wm_reply(r, "%s\n", pp.line[i]);
}

/* For "state": whose passport is up. */
void
passport_state(struct tinywl_server *s, struct mw_reply *r)
{
	struct pp_node *n;
	int windows = 0;

	if (pp.pid == 0)
		return;
	wl_list_for_each(n, &pp.shown, link)
		windows++;
	wm_reply(r, "passport %d windows %d\n", (int)pp.pid, windows);
}
