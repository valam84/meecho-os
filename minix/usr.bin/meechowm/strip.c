/*
 * The strip (stage 11.7): windows as columns on an endless horizontal
 * strip, one strip per workspace, the workspaces one above another.
 *
 * A new window opens as a column to the right of the focused one and
 * takes the focus.  The view moves along the strip as little as it must
 * to show the focused column whole (layout.c).  A column's width is a
 * preset - a third, a half, two thirds of the screen, or all of it - and
 * its height the screen's; windows never overlap, so a window that will
 * not take its column's size is cut to it.  Workspaces come and go as
 * niri's do: there is always one empty workspace at the bottom, and an
 * empty one that is left behind goes away.
 *
 * Nothing jumps: a column the layout moves goes there from where it is
 * drawn, and a workspace slides in from below or above as the one it
 * replaces slides out (MEECHOWM_ANIM_MS, 200 by default, 0 for none).
 * Every move runs on one clock, which every change restarts from where
 * things are drawn at that moment - so a key pressed mid-move turns the
 * motion, it does not jump it back; the easing is layout.c's.
 *
 * Every action is a command, wm_command(), whether it comes from a key
 * (meechowm.c) or from the socket (ipc.c): one path to test, and the one
 * a launcher or a script will take.
 *
 *   focus left|right|up|down     a column; up and down are workspaces
 *   move left|right|up|down      the focused column, along its strip or
 *                                to the workspace above or below
 *   width next|1/3|1/2|2/3|1     the focused column's width
 *   maximize                     all of the screen, or back
 *   center                       the focused column in the middle
 *   close                        ask the focused window to close
 *   workspace N                  the Nth workspace from the top
 *   spawn COMMAND                /bin/sh -c COMMAND
 *   terminal                     the terminal (MEECHOWM_TERM, foot)
 *   launcher                     the launcher (MEECHOWM_LAUNCHER)
 *   anim MS                      how long a move takes; 0: no motion
 *   state                        the layout, as text (for the stands)
 *   quit
 */
#include <sys/types.h>
#include <sys/wait.h>

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <wayland-server-core.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/edges.h>
#include <wlr/util/log.h>

#include "fb_ioc.h"
#include "layout.h"
#include "meechowm.h"
#include "wm.h"

static const float colour_background[4] = {
	0x1d / 255.0f, 0x23 / 255.0f, 0x30 / 255.0f, 1
};
static const float colour_ring[4] = {
	0x7f / 255.0f, 0xc8 / 255.0f, 0xff / 255.0f, 1
};
#define RING	2		/* the focus ring's width: it lies in the gap */

/* A layout's column widths and their x on the screen. */
static int *lay_w, *lay_x;
static int lay_cap;

static bool
lay_grow(int n)
{
	int *w, *x;

	if (n <= lay_cap)
		return true;
	if ((w = realloc(lay_w, (size_t)n * sizeof(*w))) == NULL)
		return false;
	lay_w = w;
	if ((x = realloc(lay_x, (size_t)n * sizeof(*x))) == NULL)
		return false;
	lay_x = x;
	lay_cap = n;
	return true;
}

/*===========================================================================*
 *    workspaces and columns                                                 *
 *===========================================================================*/
static struct mw_workspace *
ws_create(struct tinywl_server *s)
{
	struct mw_workspace *ws;

	if ((ws = calloc(1, sizeof(*ws))) == NULL ||
	    (ws->tree = wlr_scene_tree_create(s->layer_ws)) == NULL) {
		wlr_log(WLR_ERROR, "no memory for a workspace");
		free(ws);
		return NULL;
	}
	ws->server = s;
	wl_list_init(&ws->columns);
	wl_list_insert(s->workspaces.prev, &ws->link);
	wlr_scene_node_set_enabled(&ws->tree->node, false);
	return ws;
}

/* The workspace above (dir < 0) or below; NULL at the end. */
static struct mw_workspace *
ws_next(struct mw_workspace *ws, int dir)
{
	struct wl_list *l = dir < 0 ? ws->link.prev : ws->link.next;

	if (l == &ws->server->workspaces)
		return NULL;
	return wl_container_of(l, ws, link);
}

static int
ws_number(struct mw_workspace *ws)
{
	struct mw_workspace *w;
	int n = 0;

	wl_list_for_each(w, &ws->server->workspaces, link) {
		n++;
		if (w == ws)
			break;
	}
	return n;
}

/*
 * One empty workspace at the bottom, and no other empty one but the one
 * on the screen.
 */
static void
ws_tidy(struct tinywl_server *s)
{
	struct mw_workspace *ws, *tmp, *last;

	wl_list_for_each_safe(ws, tmp, &s->workspaces, link) {
		if (ws == s->ws || !wl_list_empty(&ws->columns) ||
		    ws->link.next == &s->workspaces)
			continue;
		wlr_scene_node_destroy(&ws->tree->node);
		wl_list_remove(&ws->link);
		free(ws);
	}
	last = wl_container_of(s->workspaces.prev, last, link);
	if (!wl_list_empty(&last->columns))
		(void)ws_create(s);
}

/* The column left (dir < 0) or right of this one; NULL at the end. */
static struct tinywl_toplevel *
col_next(struct tinywl_toplevel *t, int dir)
{
	struct wl_list *l = dir < 0 ? t->column_link.prev : t->column_link.next;

	if (l == &t->ws->columns)
		return NULL;
	return wl_container_of(l, t, column_link);
}

/*===========================================================================*
 *    layout                                                                 *
 *===========================================================================*/
/* The size the column has, asked of the window if it changed. */
static void
configure(struct tinywl_toplevel *t)
{
	if (!t->xdg_toplevel->base->initialized ||
	    (t->cfg_width == t->box.width && t->cfg_height == t->box.height))
		return;
	t->cfg_width = t->box.width;
	t->cfg_height = t->box.height;
	wlr_xdg_toplevel_set_size(t->xdg_toplevel, t->box.width,
	    t->box.height);
}

/*
 * The window into its column, where the column is drawn now (at_x - box.x
 * once it has stopped): its geometry's corner on the column's, and cut to
 * the column.  The cut is set on the window's own surfaces only - the
 * first child of the xdg surface's tree; its popups come after, and must
 * not be cut to the column they open from.
 */
static void
place(struct tinywl_toplevel *t)
{
	struct wlr_box *geo = &t->xdg_toplevel->base->geometry;
	struct wlr_box clip = { geo->x, geo->y, t->box.width, t->box.height };
	struct wlr_scene_node *own;

	wlr_scene_node_set_position(&t->scene_tree->node, t->at_x - geo->x,
	    t->box.y - geo->y);
	if (!wl_list_empty(&t->scene_tree->children)) {
		own = wl_container_of(t->scene_tree->children.next, own, link);
		wlr_scene_subsurface_tree_set_clip(own, &clip);
	}
}

static void
ring_update(struct tinywl_server *s)
{
	struct tinywl_toplevel *t = s->ws->focus;
	struct wlr_box b, side[4];
	int i;

	if (t == NULL || s->gap < RING) {
		wlr_scene_node_set_enabled(&s->ring->node, false);
		return;
	}
	b = t->box;
	b.x = t->at_x;			/* it goes where its column goes */
	side[0] = (struct wlr_box){ b.x - RING, b.y - RING,
	    b.width + 2 * RING, RING };
	side[1] = (struct wlr_box){ b.x - RING, b.y + b.height,
	    b.width + 2 * RING, RING };
	side[2] = (struct wlr_box){ b.x - RING, b.y, RING, b.height };
	side[3] = (struct wlr_box){ b.x + b.width, b.y, RING, b.height };
	for (i = 0; i < 4; i++) {
		wlr_scene_node_set_position(&s->ring_side[i]->node,
		    side[i].x, side[i].y);
		wlr_scene_rect_set_size(s->ring_side[i], side[i].width,
		    side[i].height);
	}
	wlr_scene_node_set_enabled(&s->ring->node, true);
}

/*===========================================================================*
 *    motion                                                                 *
 *===========================================================================*/
static uint32_t
now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

/*
 * The clock starts again, from where everything is drawn now: a move
 * already under way turns towards the new place instead of jumping.
 */
static void
anim_begin(struct tinywl_server *s)
{
	struct mw_workspace *ws;
	struct tinywl_toplevel *t;

	wl_list_for_each(ws, &s->workspaces, link) {
		ws->from_y = ws->at_y;
		wl_list_for_each(t, &ws->columns, column_link)
			t->from_x = t->at_x;
	}
	s->anim_start = now_ms();
	if (!s->animating) {
		s->animating = true;
		wl_event_source_timer_update(s->anim_timer, 1);
	}
}

/*
 * A step of every move: about sixty a second while anything moves, none
 * otherwise.  Positions come from the clock, not from the steps, so a
 * frame that is late (pixman on four A55, TCG) costs smoothness, not
 * time: the move ends when it should.
 */
static int
anim_tick(void *data)
{
	struct tinywl_server *s = data;
	struct mw_workspace *ws;
	struct tinywl_toplevel *t;
	long el = (long)(uint32_t)(now_ms() - s->anim_start);
	bool done = el >= s->anim_ms;

	wl_list_for_each(ws, &s->workspaces, link) {
		ws->at_y = mc_ease(ws->from_y, ws->to_y, el, s->anim_ms);
		wlr_scene_node_set_position(&ws->tree->node, 0, ws->at_y);
		if (done && ws != s->ws)	/* off the screen now */
			wlr_scene_node_set_enabled(&ws->tree->node, false);
		wl_list_for_each(t, &ws->columns, column_link) {
			t->at_x = mc_ease(t->from_x, t->box.x, el, s->anim_ms);
			place(t);
		}
	}
	ring_update(s);
	if (!done) {
		wl_event_source_timer_update(s->anim_timer, 16);
		return 0;
	}
	s->animating = false;
	wm_pointer_refresh(s);		/* what is under the pointer now */
	return 0;
}

/*
 * Every column of the workspace to its place: the view fitted to the
 * focused column (layout.c), or the focused column put in the middle.  A
 * column already drawn on a workspace that shows goes there in a move;
 * one that is new, or on a workspace out of sight, is put there at once.
 */
static void
relayout(struct mw_workspace *ws, bool centre)
{
	struct tinywl_server *s = ws->server;
	struct tinywl_toplevel *t;
	int n = wl_list_length(&ws->columns), i = 0, f = -1, h;
	bool move = false, shows = ws->tree->node.enabled;

	if (!lay_grow(n)) {
		wlr_log(WLR_ERROR, "no memory to lay out %d columns", n);
		return;
	}
	wl_list_for_each(t, &ws->columns, column_link) {
		lay_w[i] = mc_col_width(t->width, s->area.width, s->gap);
		if (t == ws->focus)
			f = i;
		i++;
	}
	if (centre && f >= 0) {
		ws->view = mc_strip_center(lay_w, n, f, s->area.width, s->gap);
		mc_strip_place(lay_w, n, ws->view, s->area.x, s->gap, lay_x);
	} else
		ws->view = mc_strip_fit(lay_w, n, f, ws->view, s->area.x,
		    s->area.width, s->gap, lay_x);

	h = s->area.height - 2 * s->gap;
	i = 0;
	wl_list_for_each(t, &ws->columns, column_link) {
		t->box = (struct wlr_box){ lay_x[i], s->area.y + s->gap,
		    lay_w[i], h > 1 ? h : 1 };
		if (s->anim_ms > 0 && shows && t->shown) {
			if (t->at_x != t->box.x)
				move = true;
		} else
			t->at_x = t->box.x;
		t->shown = true;
		configure(t);
		place(t);
		i++;
	}
	if (move)
		anim_begin(s);
	if (ws == s->ws)
		ring_update(s);
}

/* The workspace's focus, and the keyboard's if it is on the screen. */
static void
set_focus(struct mw_workspace *ws, struct tinywl_toplevel *t)
{
	struct tinywl_server *s = ws->server;

	ws->focus = t;
	relayout(ws, false);
	if (ws != s->ws)
		return;
	if (t != NULL)
		focus_toplevel(t);
	else
		wlr_seat_keyboard_notify_clear_focus(s->seat);
	wm_pointer_refresh(s);
}

/*
 * Another workspace on the screen.  The one below comes up from under the
 * screen as the old one goes up and out of it, and the other way for the
 * one above; the old one is hidden when the move is over.  The focus ring
 * lives in the tree of the workspace on the screen, under its windows, and
 * so moves with it.
 */
static void
switch_to(struct tinywl_server *s, struct mw_workspace *ws)
{
	struct mw_workspace *old = s->ws;
	int dir;

	if (ws == old)
		return;
	if (old->focus != NULL)
		wlr_xdg_toplevel_set_activated(old->focus->xdg_toplevel, false);
	dir = ws_number(ws) > ws_number(old) ? 1 : -1;
	s->ws = ws;
	if (s->anim_ms > 0) {
		if (!ws->tree->node.enabled)	/* not already on the way */
			ws->at_y = dir * s->height;
		ws->to_y = 0;
		old->to_y = -dir * s->height;
		wlr_scene_node_set_position(&ws->tree->node, 0, ws->at_y);
		wlr_scene_node_set_enabled(&ws->tree->node, true);
		anim_begin(s);
	} else {
		wlr_scene_node_set_enabled(&old->tree->node, false);
		ws->at_y = ws->to_y = 0;
		wlr_scene_node_set_position(&ws->tree->node, 0, 0);
		wlr_scene_node_set_enabled(&ws->tree->node, true);
	}
	wlr_scene_node_reparent(&s->ring->node, ws->tree);
	wlr_scene_node_lower_to_bottom(&s->ring->node);
	set_focus(ws, ws->focus);
	ws_tidy(s);
}

/*===========================================================================*
 *    windows come and go                                                    *
 *===========================================================================*/
void
strip_init(struct tinywl_server *s)
{
	int i;

	/*
	 * The scene, bottom to top: the desktop's colour, the background and
	 * bottom layers (layers.c), the workspaces, the top and overlay
	 * layers.  The cursor wlroots draws over all of it.
	 */
	wl_list_init(&s->workspaces);
	if ((s->background = wlr_scene_rect_create(&s->scene->tree, 0, 0,
	    colour_background)) == NULL ||
	    (s->layer[0] = wlr_scene_tree_create(&s->scene->tree)) == NULL ||
	    (s->layer[1] = wlr_scene_tree_create(&s->scene->tree)) == NULL ||
	    (s->ring = wlr_scene_tree_create(&s->scene->tree)) == NULL)
		goto nomem;
	for (i = 0; i < 4; i++)
		if ((s->ring_side[i] = wlr_scene_rect_create(s->ring, 0, 0,
		    colour_ring)) == NULL)
			goto nomem;
	wlr_scene_node_set_enabled(&s->ring->node, false);
	/*
	 * A window's tree is in a workspace's only while it is mapped, and
	 * here otherwise: a workspace that goes away takes its tree with it,
	 * and must not take a window that has yet to be shown, or that has
	 * hidden itself, with it.
	 */
	if ((s->hidden = wlr_scene_tree_create(&s->scene->tree)) == NULL)
		goto nomem;
	wlr_scene_node_set_enabled(&s->hidden->node, false);
	if ((s->layer_ws = wlr_scene_tree_create(&s->scene->tree)) == NULL ||
	    (s->layer[2] = wlr_scene_tree_create(&s->scene->tree)) == NULL ||
	    (s->layer[3] = wlr_scene_tree_create(&s->scene->tree)) == NULL ||
	    (s->ws = ws_create(s)) == NULL)
		goto nomem;
	wlr_scene_node_set_enabled(&s->ws->tree->node, true);
	wlr_scene_node_reparent(&s->ring->node, s->ws->tree);
	if ((s->anim_timer = wl_event_loop_add_timer(
	    wl_display_get_event_loop(s->wl_display), anim_tick, s)) == NULL)
		goto nomem;
	return;
nomem:
	wlr_log(WLR_ERROR, "no memory for the strip");
	exit(1);
}

void
strip_screen(struct tinywl_server *s, int width, int height)
{
	struct mw_workspace *ws;

	s->width = width;
	s->height = height;
	s->area = (struct wlr_box){ 0, 0, width, height };
	wlr_scene_rect_set_size(s->background, width, height);
	wl_list_for_each(ws, &s->workspaces, link)
		relayout(ws, false);
}

/*
 * What the panels leave of the screen (layers.c): columns get the height
 * of that and are laid out across its width - and get there in a move.
 */
void
strip_area(struct tinywl_server *s, const struct wlr_box *area)
{
	struct mw_workspace *ws;

	if (wlr_box_equal(&s->area, area))
		return;
	s->area = *area;
	wl_list_for_each(ws, &s->workspaces, link)
		relayout(ws, false);
	wm_pointer_refresh(s);
}

/* The first configure: the size of the column the window will have. */
void
strip_initial_commit(struct tinywl_toplevel *t)
{
	struct tinywl_server *s = t->server;
	int w = mc_col_width(t->width, s->area.width, s->gap),
	    h = s->area.height - 2 * s->gap;

	wlr_xdg_toplevel_set_tiled(t->xdg_toplevel, WLR_EDGE_TOP |
	    WLR_EDGE_BOTTOM | WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
	wlr_xdg_toplevel_set_size(t->xdg_toplevel, w, h);
	t->cfg_width = w;
	t->cfg_height = h;
}

/* A new column, right of the focused one on the workspace on the screen. */
void
strip_map(struct tinywl_toplevel *t)
{
	struct tinywl_server *s = t->server;
	struct mw_workspace *ws = s->ws;

	if (ws->focus != NULL)
		wl_list_insert(&ws->focus->column_link, &t->column_link);
	else
		wl_list_insert(ws->columns.prev, &t->column_link);
	t->ws = ws;
	wlr_scene_node_reparent(&t->scene_tree->node, ws->tree);
	set_focus(ws, t);
	ws_tidy(s);
}

/* The column goes; the focus to its right, or else to its left. */
void
strip_unmap(struct tinywl_toplevel *t)
{
	struct mw_workspace *ws = t->ws;
	struct tinywl_toplevel *next;

	if (ws == NULL)
		return;
	if ((next = col_next(t, 1)) == NULL)
		next = col_next(t, -1);
	wl_list_remove(&t->column_link);
	t->ws = NULL;
	t->shown = false;		/* shown again, it appears in place */
	wlr_scene_node_reparent(&t->scene_tree->node, ws->server->hidden);
	set_focus(ws, ws->focus == t ? next : ws->focus);
	ws_tidy(ws->server);
}

void
strip_commit(struct tinywl_toplevel *t)
{
	if (t->ws != NULL)
		place(t);
}

/* A click: the column under the pointer takes the focus. */
void
strip_focus(struct tinywl_toplevel *t)
{
	if (t->ws == NULL)
		return;
	if (t->ws != t->server->ws) {
		t->ws->focus = t;
		switch_to(t->server, t->ws);
	} else
		set_focus(t->ws, t);
}

void
strip_maximize(struct tinywl_toplevel *t)
{
	if (t->ws == NULL)
		return;
	if (t->width == MC_W_FULL)
		t->width = t->width_was == MC_W_FULL ? MC_W_HALF : t->width_was;
	else {
		t->width_was = t->width;
		t->width = MC_W_FULL;
	}
	relayout(t->ws, false);
}

/*===========================================================================*
 *    commands                                                               *
 *===========================================================================*/
void
wm_reply(struct mw_reply *r, const char *fmt, ...)
{
	va_list ap;
	size_t room;
	int n;

	if (r == NULL || r->len + 1 >= sizeof(r->buf))
		return;
	room = sizeof(r->buf) - r->len;
	va_start(ap, fmt);
	n = vsnprintf(r->buf + r->len, room, fmt, ap);
	va_end(ap);
	if (n > 0)
		r->len += (size_t)n < room ? (size_t)n : room - 1;
}

/*
 * /bin/sh -c cmd, in a session of its own and as nobody's child: the
 * middle process exits at once and is waited for here, so that nothing
 * the compositor starts is left a zombie.
 *
 * The command makes the session itself, not the middle process: PM hangs
 * up the process group of a session leader when it exits - with a
 * terminal or without one (servers/pm/forkexit.c), where POSIX does it
 * only for a terminal's controlling process.  A middle process that had
 * made itself a leader killed the command it had just started, every
 * time, and without a word.
 */
void
wm_spawn(const char *cmd)
{
	pid_t pid;

	if ((pid = fork()) < 0) {
		wlr_log_errno(WLR_ERROR, "fork");
		return;
	}
	if (pid == 0) {
		if (fork() == 0) {
			setsid();
			execl("/bin/sh", "/bin/sh", "-c", cmd, (char *)NULL);
			_exit(127);
		}
		_exit(0);
	}
	while (waitpid(pid, NULL, 0) < 0 && errno == EINTR)
		;
}

static void
state(struct tinywl_server *s, struct mw_reply *r)
{
	struct mw_workspace *ws;
	struct tinywl_toplevel *t;
	struct wlr_box *g;
	pid_t pid;
	uid_t uid;
	gid_t gid;
	int wi = 0, ci;

	wm_reply(r, "screen %dx%d area %d,%d %dx%d gap %d workspaces %d "
	    "active %d animating %d\n", s->width, s->height, s->area.x,
	    s->area.y, s->area.width, s->area.height, s->gap,
	    wl_list_length(&s->workspaces), ws_number(s->ws),
	    s->animating ? 1 : 0);
	wl_list_for_each(ws, &s->workspaces, link) {
		wm_reply(r, "workspace %d %s view %d columns %d y %d\n", ++wi,
		    ws == s->ws ? "active" : "-", ws->view,
		    wl_list_length(&ws->columns), ws->at_y);
		ci = 0;
		wl_list_for_each(t, &ws->columns, column_link) {
			g = &t->xdg_toplevel->base->geometry;
			pid = 0;
			wl_client_get_credentials(
			    t->xdg_toplevel->base->client->client, &pid, &uid,
			    &gid);
			/* x is where the column goes, "at" where it is drawn
			 * now: the two differ only while it moves. */
			wm_reply(r, "column %d.%d %d %d %d %d width %s %s at %d "
			    "size %dx%d pid %d title %s\n", wi, ++ci, t->box.x,
			    t->box.y, t->box.width, t->box.height,
			    mc_width_name(t->width),
			    t == ws->focus ? "focus" : "-", t->at_x, g->width,
			    g->height, (int)pid, t->xdg_toplevel->title != NULL ?
			    t->xdg_toplevel->title : "-");
		}
	}
	layers_state(s, r);
}

/* "left" and "up" are -1, "right" and "down" +1, anything else 0. */
static int
direction(const char *w, bool *vertical)
{
	*vertical = strcmp(w, "up") == 0 || strcmp(w, "down") == 0;
	if (strcmp(w, "left") == 0 || strcmp(w, "up") == 0)
		return -1;
	if (strcmp(w, "right") == 0 || strcmp(w, "down") == 0)
		return 1;
	return 0;
}

static void
cmd_focus(struct tinywl_server *s, int dir, bool vertical)
{
	struct mw_workspace *ws;
	struct tinywl_toplevel *t;

	if (vertical) {
		if ((ws = ws_next(s->ws, dir)) != NULL)
			switch_to(s, ws);
	} else if (s->ws->focus != NULL &&
	    (t = col_next(s->ws->focus, dir)) != NULL)
		set_focus(s->ws, t);
}

static void
cmd_move(struct tinywl_server *s, int dir, bool vertical)
{
	struct mw_workspace *from = s->ws, *to;
	struct tinywl_toplevel *t = from->focus, *n;

	if (t == NULL)
		return;
	if (!vertical) {
		if ((n = col_next(t, dir)) == NULL)
			return;
		wl_list_remove(&t->column_link);
		wl_list_insert(dir < 0 ? n->column_link.prev : &n->column_link,
		    &t->column_link);
		relayout(from, false);
		wm_pointer_refresh(s);
		return;
	}
	if ((to = ws_next(from, dir)) == NULL)
		return;
	/* Out of this strip, the focus to its neighbour... */
	if ((n = col_next(t, 1)) == NULL)
		n = col_next(t, -1);
	wl_list_remove(&t->column_link);
	from->focus = n;
	relayout(from, false);
	/* ...into that one right of its focus, and the view follows it. */
	if (to->focus != NULL)
		wl_list_insert(&to->focus->column_link, &t->column_link);
	else
		wl_list_insert(to->columns.prev, &t->column_link);
	t->ws = to;
	to->focus = t;
	wlr_scene_node_reparent(&t->scene_tree->node, to->tree);
	switch_to(s, to);
}

static void
cmd_width(struct tinywl_server *s, const char *arg, struct mw_reply *r)
{
	struct tinywl_toplevel *t = s->ws->focus;
	int p;

	if (strcmp(arg, "next") == 0)
		p = t == NULL || t->width == MC_W_FULL ? MC_W_THIRD :
		    (t->width + 1) % MC_W_FULL;
	else if ((p = mc_width_parse(arg)) < 0) {
		r->error = true;
		wm_reply(r, "error: width %s: not 1/3, 1/2, 2/3, 1 or next\n",
		    arg);
		return;
	}
	if (t == NULL)
		return;
	t->width = p;
	relayout(s->ws, false);
	wm_pointer_refresh(s);
}

static void
cmd_workspace(struct tinywl_server *s, const char *arg, struct mw_reply *r)
{
	struct mw_workspace *ws;
	char *end;
	long n = strtol(arg, &end, 10);

	if (*arg != '\0' && *end == '\0' && n >= 1)
		wl_list_for_each(ws, &s->workspaces, link)
			if (--n == 0) {
				switch_to(s, ws);
				return;
			}
	r->error = true;
	wm_reply(r, "error: workspace %s: there are %d\n", arg,
	    wl_list_length(&s->workspaces));
}

/* "modes": what the screen can be given, the one it has marked "current". */
static void
cmd_modes(struct tinywl_server *s, struct mw_reply *r)
{
	struct fb_modes ms;
	uint32_t i;

	if (s->fb == NULL || mc_fb_modes(s->fb, &ms) != 0) {
		r->error = true;
		wm_reply(r, "error: modes: %s\n", s->fb == NULL ? "no screen" :
		    strerror(errno));
		return;
	}
	for (i = 0; i < ms.n; i++)
		wm_reply(r, "%ux%u@%u%s\n", ms.mode[i].width, ms.mode[i].height,
		    ms.mode[i].hz, i == ms.current ? " current" : "");
}

/* "mode WIDTHxHEIGHT" or "mode WIDTHxHEIGHT@HZ": one of "modes". */
static void
cmd_mode(struct tinywl_server *s, const char *arg, struct mw_reply *r)
{
	unsigned long w, h = 0, hz = 0;
	char *e;

	w = strtoul(arg, &e, 10);
	if (*e == 'x') {
		h = strtoul(e + 1, &e, 10);
		if (*e == '@')
			hz = strtoul(e + 1, &e, 10);
	}
	if (*e != '\0' || w == 0 || h == 0 || w > 8192 || h > 8192 ||
	    hz > 1000) {
		r->error = true;
		wm_reply(r, "error: mode %s: not WIDTHxHEIGHT[@HZ]\n", arg);
		return;
	}
	if (!wm_set_mode(s, (unsigned)w, (unsigned)h, (unsigned)hz)) {
		r->error = true;
		wm_reply(r, "error: mode %s: %s\n", arg, errno == EINVAL ?
		    "not a mode of the screen (see modes)" : strerror(errno));
	}
}

/* A word of p, up to len characters, into w; false if it is longer. */
static bool
word(const char *p, size_t len, char *w, size_t size)
{
	if (len >= size)
		return false;
	memcpy(w, p, len);
	w[len] = '\0';
	return true;
}

void
wm_command(struct tinywl_server *s, const char *line, struct mw_reply *r)
{
	const char *p, *rest;
	char verb[16], arg[16], cmd[1024];
	size_t n;
	bool vertical;
	int dir;

	p = line + strspn(line, " \t");
	n = strcspn(p, " \t\r\n");
	rest = p + n + strspn(p + n, " \t");
	if (!word(p, n, verb, sizeof(verb)))
		goto unknown;
	if (strcmp(verb, "spawn") == 0) {
		if (!word(rest, strcspn(rest, "\r\n"), cmd, sizeof(cmd)) ||
		    cmd[0] == '\0')
			goto unknown;
		wlr_log(WLR_DEBUG, "command: spawn %s", cmd);
		wm_spawn(cmd);
		wm_reply(r, "ok\n");
		return;
	}
	if (!word(rest, strcspn(rest, " \t\r\n"), arg, sizeof(arg)))
		goto unknown;
	wlr_log(WLR_DEBUG, "command: %s %s", verb, arg);

	if (strcmp(verb, "focus") == 0 || strcmp(verb, "move") == 0) {
		if ((dir = direction(arg, &vertical)) == 0)
			goto unknown;
		if (verb[0] == 'f')
			cmd_focus(s, dir, vertical);
		else
			cmd_move(s, dir, vertical);
	} else if (strcmp(verb, "width") == 0) {
		cmd_width(s, arg, r);
		if (r->error)
			return;
	} else if (strcmp(verb, "maximize") == 0) {
		if (s->ws->focus != NULL) {
			strip_maximize(s->ws->focus);
			wm_pointer_refresh(s);
		}
	} else if (strcmp(verb, "center") == 0) {
		if (s->ws->focus != NULL) {
			relayout(s->ws, true);
			wm_pointer_refresh(s);
		}
	} else if (strcmp(verb, "close") == 0) {
		if (s->ws->focus != NULL)
			wlr_xdg_toplevel_send_close(s->ws->focus->xdg_toplevel);
	} else if (strcmp(verb, "workspace") == 0) {
		cmd_workspace(s, arg, r);
		if (r->error)
			return;
	} else if (strcmp(verb, "terminal") == 0)
		wm_spawn(s->terminal);
	else if (strcmp(verb, "launcher") == 0)
		wm_spawn(s->launcher);
	else if (strcmp(verb, "anim") == 0) {
		char *end;
		long ms = strtol(arg, &end, 10);

		if (arg[0] == '\0' || *end != '\0' || ms < 0 || ms > 60000) {
			r->error = true;
			wm_reply(r, "error: anim %s: milliseconds, 0 to 60000\n",
			    arg);
			return;
		}
		s->anim_ms = (int)ms;
	}
	else if (strcmp(verb, "state") == 0) {
		state(s, r);
		return;
	} else if (strcmp(verb, "modes") == 0) {
		cmd_modes(s, r);
		return;
	} else if (strcmp(verb, "mode") == 0) {
		cmd_mode(s, arg, r);
		if (r->error)
			return;
	} else if (strcmp(verb, "quit") == 0)
		wl_display_terminate(s->wl_display);
	else
		goto unknown;
	wm_reply(r, "ok\n");
	return;
unknown:
	r->error = true;
	wm_reply(r, "error: %.200s: no such command\n", line);
}
