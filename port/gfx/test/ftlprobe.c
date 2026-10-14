/*
 * ftlprobe - what a task bar sees of meechowm's windows (stage 11.7):
 * wlr-foreign-toplevel-management, from the client's side.
 *
 *   ftlprobe                 the windows, a line each:
 *                            "toplevel TITLE app APP_ID state S outputs N"
 *                            (S: activated, maximized, comma-separated, or -)
 *   ftlprobe -a TITLE        ask for TITLE to be activated
 *   ftlprobe -c TITLE        ask for it to be closed
 *   ftlprobe -m TITLE        ask for it to be maximized; -u: unmaximized
 *   ftlprobe -w SECONDS      then watch that long: every change a line,
 *                            "closed TITLE", "title OLD NEW", "state ..."
 *
 * Prints "FTLPROBE n toplevel(s)" at the end; exit 1 if the compositor
 * has no manager or TITLE is not among the windows.
 */
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <wayland-client.h>

#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"

#define MAX_TOPLEVELS	64

struct toplevel {
	struct zwlr_foreign_toplevel_handle_v1 *h;
	char title[256], app_id[128];
	bool activated, maximized, closed, done;
	int outputs;
};

static struct zwlr_foreign_toplevel_manager_v1 *manager;
static struct wl_seat *seat;
static struct toplevel tl[MAX_TOPLEVELS];
static int ntl;
static bool watching;

static const char *
state_str(const struct toplevel *t)
{
	static char s[64];

	s[0] = '\0';
	if (t->activated)
		strcat(s, "activated");
	if (t->maximized)
		strcat(s, s[0] ? ",maximized" : "maximized");
	return s[0] ? s : "-";
}

static void
h_title(void *data, struct zwlr_foreign_toplevel_handle_v1 *h,
	const char *title)
{
	struct toplevel *t = data;

	(void)h;
	if (watching && strcmp(t->title, title) != 0)
		printf("title %s %s\n", t->title[0] ? t->title : "-", title);
	snprintf(t->title, sizeof(t->title), "%s", title);
}

static void
h_app_id(void *data, struct zwlr_foreign_toplevel_handle_v1 *h,
	const char *app_id)
{
	struct toplevel *t = data;

	(void)h;
	snprintf(t->app_id, sizeof(t->app_id), "%s", app_id);
}

static void
h_output_enter(void *data, struct zwlr_foreign_toplevel_handle_v1 *h,
	struct wl_output *o)
{
	(void)h;
	(void)o;
	((struct toplevel *)data)->outputs++;
}

static void
h_output_leave(void *data, struct zwlr_foreign_toplevel_handle_v1 *h,
	struct wl_output *o)
{
	(void)h;
	(void)o;
	((struct toplevel *)data)->outputs--;
}

static void
h_state(void *data, struct zwlr_foreign_toplevel_handle_v1 *h,
	struct wl_array *state)
{
	struct toplevel *t = data;
	uint32_t *s;
	bool was_a = t->activated, was_m = t->maximized;

	(void)h;
	t->activated = t->maximized = false;
	wl_array_for_each(s, state) {
		if (*s == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED)
			t->activated = true;
		if (*s == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MAXIMIZED)
			t->maximized = true;
	}
	if (watching && t->done &&
	    (was_a != t->activated || was_m != t->maximized))
		printf("state %s %s\n", t->title[0] ? t->title : "-",
		    state_str(t));
}

static void
h_done(void *data, struct zwlr_foreign_toplevel_handle_v1 *h)
{
	(void)h;
	((struct toplevel *)data)->done = true;
}

static void
h_closed(void *data, struct zwlr_foreign_toplevel_handle_v1 *h)
{
	struct toplevel *t = data;

	if (watching)
		printf("closed %s\n", t->title[0] ? t->title : "-");
	t->closed = true;
	zwlr_foreign_toplevel_handle_v1_destroy(h);
}

static void
h_parent(void *data, struct zwlr_foreign_toplevel_handle_v1 *h,
	struct zwlr_foreign_toplevel_handle_v1 *parent)
{
	(void)data;
	(void)h;
	(void)parent;
}

static const struct zwlr_foreign_toplevel_handle_v1_listener h_listener = {
	.title = h_title,
	.app_id = h_app_id,
	.output_enter = h_output_enter,
	.output_leave = h_output_leave,
	.state = h_state,
	.done = h_done,
	.closed = h_closed,
	.parent = h_parent,
};

static void
m_toplevel(void *data, struct zwlr_foreign_toplevel_manager_v1 *m,
	struct zwlr_foreign_toplevel_handle_v1 *h)
{
	struct toplevel *t;

	(void)data;
	(void)m;
	if (ntl >= MAX_TOPLEVELS) {
		zwlr_foreign_toplevel_handle_v1_destroy(h);
		return;
	}
	t = &tl[ntl++];
	memset(t, 0, sizeof(*t));
	t->h = h;
	zwlr_foreign_toplevel_handle_v1_add_listener(h, &h_listener, t);
	if (watching)
		printf("new toplevel\n");
}

static void
m_finished(void *data, struct zwlr_foreign_toplevel_manager_v1 *m)
{
	(void)data;
	(void)m;
}

static const struct zwlr_foreign_toplevel_manager_v1_listener m_listener = {
	.toplevel = m_toplevel,
	.finished = m_finished,
};

static void
g_global(void *data, struct wl_registry *r, uint32_t name,
	const char *iface, uint32_t version)
{
	(void)data;
	if (strcmp(iface, zwlr_foreign_toplevel_manager_v1_interface.name) ==
	    0) {
		manager = wl_registry_bind(r, name,
		    &zwlr_foreign_toplevel_manager_v1_interface,
		    version < 3 ? version : 3);
		zwlr_foreign_toplevel_manager_v1_add_listener(manager,
		    &m_listener, NULL);
	} else if (strcmp(iface, wl_seat_interface.name) == 0 && seat == NULL)
		seat = wl_registry_bind(r, name, &wl_seat_interface, 1);
	else if (strcmp(iface, wl_output_interface.name) == 0)
		/* output_enter names the client's own wl_output: none
		 * bound, none told - a task bar binds them anyway. */
		(void)wl_registry_bind(r, name, &wl_output_interface, 1);
}

static void
g_remove(void *data, struct wl_registry *r, uint32_t name)
{
	(void)data;
	(void)r;
	(void)name;
}

static const struct wl_registry_listener g_listener = {
	.global = g_global,
	.global_remove = g_remove,
};

static struct toplevel *
find(const char *title)
{
	int i;

	for (i = 0; i < ntl; i++)
		if (!tl[i].closed && strcmp(tl[i].title, title) == 0)
			return &tl[i];
	return NULL;
}

int
main(int argc, char **argv)
{
	struct wl_display *d;
	struct toplevel *t = NULL;
	const char *title = NULL;
	int op = 0, c, i, n, wait_s = 0;
	struct timespec t0, now;

	while ((c = getopt(argc, argv, "a:c:m:u:w:")) != -1) {
		switch (c) {
		case 'a': case 'c': case 'm': case 'u':
			op = c;
			title = optarg;
			break;
		case 'w':
			wait_s = atoi(optarg);
			break;
		default:
			fprintf(stderr, "usage: ftlprobe [-a|-c|-m|-u title] "
			    "[-w seconds]\n");
			return 2;
		}
	}
	if ((d = wl_display_connect(NULL)) == NULL) {
		fprintf(stderr, "ftlprobe: no display\n");
		return 1;
	}
	wl_registry_add_listener(wl_display_get_registry(d), &g_listener, NULL);
	wl_display_roundtrip(d);
	if (manager == NULL) {
		printf("FTLPROBE no zwlr_foreign_toplevel_manager_v1\n");
		return 1;
	}
	wl_display_roundtrip(d);
	wl_display_roundtrip(d);

	for (i = 0, n = 0; i < ntl; i++) {
		if (tl[i].closed)
			continue;
		n++;
		printf("toplevel %s app %s state %s outputs %d\n",
		    tl[i].title[0] ? tl[i].title : "-",
		    tl[i].app_id[0] ? tl[i].app_id : "-", state_str(&tl[i]),
		    tl[i].outputs);
	}

	if (op != 0) {
		if ((t = find(title)) == NULL) {
			printf("FTLPROBE %s: no such toplevel\n", title);
			return 1;
		}
		switch (op) {
		case 'a':
			zwlr_foreign_toplevel_handle_v1_activate(t->h, seat);
			break;
		case 'c':
			zwlr_foreign_toplevel_handle_v1_close(t->h);
			break;
		case 'm':
			zwlr_foreign_toplevel_handle_v1_set_maximized(t->h);
			break;
		case 'u':
			zwlr_foreign_toplevel_handle_v1_unset_maximized(t->h);
			break;
		}
		wl_display_roundtrip(d);
		printf("asked: %c %s\n", op, title);
	}

	if (wait_s > 0) {
		watching = true;
		fflush(stdout);
		clock_gettime(CLOCK_MONOTONIC, &t0);
		for (;;) {
			struct pollfd p = { wl_display_get_fd(d), POLLIN, 0 };

			clock_gettime(CLOCK_MONOTONIC, &now);
			if (now.tv_sec - t0.tv_sec >= wait_s)
				break;
			wl_display_flush(d);
			if (poll(&p, 1, 250) > 0 &&
			    wl_display_dispatch(d) < 0)
				break;
			wl_display_dispatch_pending(d);
			fflush(stdout);
		}
	}
	for (i = 0, n = 0; i < ntl; i++)
		if (!tl[i].closed)
			n++;
	printf("FTLPROBE %d toplevel(s)\n", n);
	wl_display_disconnect(d);
	return 0;
}
