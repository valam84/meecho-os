/*
 * The overview (stage 11.7): the camera pulled back from the strip.
 *
 * Everything is drawn smaller about the middle of the screen - the
 * workspace on the screen in the middle, the ones above and below it in
 * the places they have, each strip with its columns where its view puts
 * them - so that what is where can be seen at once, as niri's overview
 * does it.  The camera goes back and forth on the strip's own clock and
 * easing (strip.c), so it can be turned mid-way; at full size the picture
 * is the strip's, and the strip's own trees take over again.
 *
 * wlroots' scene does not scale a tree, so the overview is a tree of its
 * own: a scene buffer for each window, showing the window's buffer cut to
 * its column and scaled to where the camera puts the column - the
 * window's own surface, not its popups or subsurfaces - a card under each
 * workspace and the focus ring.  The workspaces' trees are hidden while it
 * shows.  Each window's buffer follows its commits.
 *
 * In the overview the keys still move the focus (Mod with the arrows,
 * and the arrows alone), Return and Escape go back to the strip, and a
 * click on a window focuses it and goes back; a click on a workspace's
 * card goes to that workspace.  Clients get no pointer: what is under it
 * is a picture of them.
 */
#include <stdio.h>
#include <stdlib.h>

#include <wayland-server-core.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>

#include "layout.h"
#include "style.h"
#include "wm.h"

#define RGBA(c)	{ ((c) >> 16 & 0xff) / 255.0f, ((c) >> 8 & 0xff) / 255.0f, \
		  ((c) & 0xff) / 255.0f, 1 }
static const float colour_card[4] = RGBA(STYLE_SURFACE);
static const float colour_ring[4] = RGBA(STYLE_ACCENT);
#define RING	2

/* The camera's size in the overview, per mille of the screen's. */
#define OVERVIEW_ZOOM	500

static struct wlr_scene_rect *ring_side[4];

bool
overview_on(const struct tinywl_server *s)
{
	return s->overview;
}

/* The zoom the camera is going to: the overview's, or full size. */
int
overview_target(const struct tinywl_server *s)
{
	return s->overview ? OVERVIEW_ZOOM : 1000;
}

/* A box of the strip's coordinates where the camera draws it. */
static struct wlr_box
camera(const struct tinywl_server *s, int x, int y, int w, int h)
{
	int cx = s->area.x + s->area.width / 2, cy = s->area.y +
	    s->area.height / 2, x0, y0, x1, y1;

	x0 = mc_zoom(x, cx, s->zoom);
	y0 = mc_zoom(y, cy, s->zoom);
	x1 = mc_zoom(x + w, cx, s->zoom);
	y1 = mc_zoom(y + h, cy, s->zoom);
	return (struct wlr_box){ x0, y0, x1 - x0 > 1 ? x1 - x0 : 1,
	    y1 - y0 > 1 ? y1 - y0 : 1 };
}

/* The window's current picture: its surface's buffer, or none yet. */
static struct wlr_buffer *
picture(struct tinywl_toplevel *t)
{
	struct wlr_surface *surface = t->xdg_toplevel->base->surface;

	return surface->buffer != NULL ? &surface->buffer->base : NULL;
}

/* What of the picture shows: its geometry, cut to the column. */
static void
source(struct tinywl_toplevel *t, struct wlr_buffer *b)
{
	struct wlr_box *geo = &t->xdg_toplevel->base->geometry;
	struct wlr_fbox box;

	if (b == NULL)
		return;
	box.x = geo->x > 0 && geo->x < b->width ? geo->x : 0;
	box.y = geo->y > 0 && geo->y < b->height ? geo->y : 0;
	box.width = geo->width > 0 ? geo->width : b->width;
	box.height = geo->height > 0 ? geo->height : b->height;
	if (box.width > t->box.width)
		box.width = t->box.width;
	if (box.height > t->box.height)
		box.height = t->box.height;
	if (box.x + box.width > b->width)
		box.width = b->width - box.x;
	if (box.y + box.height > b->height)
		box.height = b->height - box.y;
	if (box.width <= 0 || box.height <= 0)
		return;
	wlr_scene_buffer_set_source_box(t->ov_buf, &box);
}

/*
 * The overview's tree drawn for the camera as it is now: a card for every
 * workspace, every column's picture, the ring around the focused one.
 */
void
overview_place(struct tinywl_server *s)
{
	struct mw_workspace *ws;
	struct tinywl_toplevel *t;
	struct wlr_box b, f = { 0, 0, 1, 1 }, side[4];
	int active = 0, k, row, i;
	bool ring = false;

	if (s->ov == NULL)
		return;
	k = 0;
	wl_list_for_each(ws, &s->workspaces, link) {
		k++;
		if (ws == s->ws)
			active = k;
	}
	k = 0;
	wl_list_for_each(ws, &s->workspaces, link) {
		k++;
		row = (k - active) * s->height;
		if (ws->ov_card == NULL &&
		    (ws->ov_card = wlr_scene_rect_create(s->ov, 1, 1,
		    colour_card)) != NULL)
			ws->ov_card->node.data = ws;
		if (ws->ov_card != NULL) {
			b = camera(s, s->area.x, row + s->area.y, s->area.width,
			    s->area.height);
			wlr_scene_node_set_position(&ws->ov_card->node, b.x,
			    b.y);
			wlr_scene_rect_set_size(ws->ov_card, b.width, b.height);
			wlr_scene_node_lower_to_bottom(&ws->ov_card->node);
		}
		wl_list_for_each(t, &ws->columns, column_link) {
			if (t->ov_buf == NULL) {
				if ((t->ov_buf = wlr_scene_buffer_create(s->ov,
				    picture(t))) == NULL)
					continue;
				t->ov_buf->node.data = t;
			}
			source(t, picture(t));
			b = camera(s, t->at_x, row + t->box.y, t->box.width,
			    t->box.height);
			wlr_scene_buffer_set_dest_size(t->ov_buf, b.width,
			    b.height);
			wlr_scene_node_set_position(&t->ov_buf->node, b.x, b.y);
			if (ws == s->ws && t == ws->focus) {
				f = b;
				ring = true;
			}
		}
	}
	for (i = 0; i < 4; i++)
		if (ring_side[i] == NULL &&
		    (ring_side[i] = wlr_scene_rect_create(s->ov, 1, 1,
		    colour_ring)) == NULL)
			ring = false;
	if (ring) {
		side[0] = (struct wlr_box){ f.x - RING, f.y - RING,
		    f.width + 2 * RING, RING };
		side[1] = (struct wlr_box){ f.x - RING, f.y + f.height,
		    f.width + 2 * RING, RING };
		side[2] = (struct wlr_box){ f.x - RING, f.y, RING, f.height };
		side[3] = (struct wlr_box){ f.x + f.width, f.y, RING, f.height };
		for (i = 0; i < 4; i++) {
			wlr_scene_node_set_position(&ring_side[i]->node,
			    side[i].x, side[i].y);
			wlr_scene_rect_set_size(ring_side[i], side[i].width,
			    side[i].height);
			wlr_scene_node_raise_to_top(&ring_side[i]->node);
		}
	}
	for (i = 0; i < 4; i++)
		if (ring_side[i] != NULL)
			wlr_scene_node_set_enabled(&ring_side[i]->node, ring);
}

/* Back to the strip's own trees: the overview's goes. */
static void
leave(struct tinywl_server *s)
{
	struct mw_workspace *ws;
	struct tinywl_toplevel *t;
	int i;

	wl_list_for_each(ws, &s->workspaces, link) {
		ws->ov_card = NULL;
		wl_list_for_each(t, &ws->columns, column_link)
			t->ov_buf = NULL;
	}
	for (i = 0; i < 4; i++)
		ring_side[i] = NULL;
	if (s->ov != NULL)
		wlr_scene_node_destroy(&s->ov->node);	/* and all in it */
	s->ov = NULL;
	wlr_scene_node_set_enabled(&s->layer_ws->node, true);
	wm_pointer_refresh(s);
}

/*
 * A step of the camera, from strip.c's clock: where it is, the overview's
 * tree to it, and at full size on the way back, the strip's trees again.
 */
void
overview_tick(struct tinywl_server *s, bool done)
{
	if (s->ov == NULL)
		return;
	overview_place(s);
	if (done && !s->overview && s->zoom == 1000)
		leave(s);
}

/* on: 1 into the overview, 0 out of it, -1 the other way. */
void
overview_set(struct tinywl_server *s, int on)
{
	bool want = on < 0 ? !s->overview : on != 0;

	if (want == s->overview)
		return;
	s->overview = want;
	wlr_log(WLR_DEBUG, "overview %s", want ? "on" : "off");
	if (want && s->ov == NULL) {
		if ((s->ov = wlr_scene_tree_create(&s->scene->tree)) == NULL) {
			wlr_log(WLR_ERROR, "no memory for the overview");
			s->overview = false;
			return;
		}
		wlr_scene_node_place_above(&s->ov->node, &s->layer_ws->node);
		wlr_scene_node_set_enabled(&s->layer_ws->node, false);
		s->zoom = 1000;
	}
	strip_animate(s);
	wm_pointer_refresh(s);
}

/* The window's picture changed (strip_commit). */
void
overview_commit(struct tinywl_toplevel *t)
{
	if (t->ov_buf == NULL)
		return;
	wlr_scene_buffer_set_buffer(t->ov_buf, picture(t));
	source(t, picture(t));
}

/* The window goes: its picture with it. */
void
overview_unmap(struct tinywl_toplevel *t)
{
	if (t->ov_buf == NULL)
		return;
	wlr_scene_node_destroy(&t->ov_buf->node);
	t->ov_buf = NULL;
}

/* The workspace goes (ws_tidy): its card with it. */
void
overview_ws_gone(struct mw_workspace *ws)
{
	if (ws->ov_card == NULL)
		return;
	wlr_scene_node_destroy(&ws->ov_card->node);
	ws->ov_card = NULL;
}

/*
 * A click in the overview: on a window, that window and back to the
 * strip; on a workspace's card, that workspace.  True if it was the
 * overview's to take.
 */
bool
overview_click(struct tinywl_server *s, double x, double y)
{
	static struct mw_reply reply;
	struct wlr_scene_node *node;
	struct mw_workspace *ws, *w;
	double nx, ny;
	char cmd[32];
	int n;

	if (!s->overview || s->ov == NULL)
		return false;
	node = wlr_scene_node_at(&s->ov->node, x, y, &nx, &ny);
	if (node == NULL || node->data == NULL)
		return true;
	if (node->type == WLR_SCENE_NODE_BUFFER) {
		struct tinywl_toplevel *t = node->data;

		strip_focus(t);
	} else {
		ws = node->data;
		n = 0;
		wl_list_for_each(w, &s->workspaces, link) {
			n++;
			if (w == ws)
				break;
		}
		snprintf(cmd, sizeof(cmd), "workspace %d", n);
		reply.len = 0;
		reply.error = false;
		wm_command(s, cmd, &reply);
	}
	overview_set(s, 0);
	return true;
}
