/*
 * Layer surfaces (wlr-layer-shell, stage 11.7): what is not a window - a
 * panel (yambar), a launcher (fuzzel), a wallpaper - placed by its anchors
 * and margins on one of four layers: background and bottom under the
 * strip, top and overlay over it.
 *
 * A surface's exclusive zone (a panel's height) is taken off the area the
 * strip lays its columns out in, so that no column goes under the panel.
 * Exclusive zones are counted first, from the overlay down, and the other
 * surfaces are placed in what they leave - as sway does.
 *
 * The keyboard: a surface on the top or overlay layer that asks for it
 * exclusively (a launcher) has it from the moment it is mapped until it is
 * unmapped, and a click on a column does not take it away; one that asks
 * on demand gets it when clicked, and loses it to the next click on a
 * column.  Either way it goes back to the focused column after.
 */
#include <stdlib.h>

#include <wayland-server-core.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>

#include "wm.h"

struct mw_layer {
	struct wl_list link;		/* tinywl_server.layer_surfaces */
	struct tinywl_server *server;
	struct wlr_layer_surface_v1 *wlr;
	struct wlr_scene_layer_surface_v1 *scene;
	enum zwlr_layer_shell_v1_layer layer;	/* the tree it is in */
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener commit;
	struct wl_listener destroy;
	struct wl_listener new_popup;
};

static bool
exclusive(const struct mw_layer *l)
{
	return l->wlr->current.keyboard_interactive ==
	    ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE &&
	    l->wlr->current.layer >= ZWLR_LAYER_SHELL_V1_LAYER_TOP;
}

/* The keyboard to a layer surface. */
static void
give_keyboard(struct mw_layer *l)
{
	struct tinywl_server *s = l->server;
	struct wlr_keyboard *kb = wlr_seat_get_keyboard(s->seat);

	s->layer_focus = l;
	if (kb != NULL)
		wlr_seat_keyboard_notify_enter(s->seat, l->wlr->surface,
		    kb->keycodes, kb->num_keycodes, &kb->modifiers);
	else
		wlr_seat_keyboard_notify_enter(s->seat, l->wlr->surface, NULL,
		    0, NULL);
}

/* ...and back to the focused column, or to nobody. */
static void
take_keyboard(struct tinywl_server *s)
{
	s->layer_focus = NULL;
	if (s->ws->focus != NULL)
		focus_toplevel(s->ws->focus);
	else
		wlr_seat_keyboard_notify_clear_focus(s->seat);
}

bool
layers_hold_keyboard(struct tinywl_server *s)
{
	return s->layer_focus != NULL && exclusive(s->layer_focus);
}

void
layers_click(struct tinywl_server *s, struct wlr_surface *surface)
{
	struct wlr_layer_surface_v1 *wlr;
	struct mw_layer *l;

	if (surface == NULL ||
	    (wlr = wlr_layer_surface_v1_try_from_wlr_surface(surface)) == NULL)
		return;
	wl_list_for_each(l, &s->layer_surfaces, link)
		if (l->wlr == wlr && wlr->current.keyboard_interactive !=
		    ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE) {
			give_keyboard(l);
			return;
		}
}

/*
 * Every layer surface to its place on the screen, and what is left of the
 * screen to the strip.  A surface that is not mapped yet is still told its
 * size (it maps only once it has had one), but its zone does not count.
 */
void
layers_arrange(struct tinywl_server *s)
{
	static const enum zwlr_layer_shell_v1_layer order[] = {
		ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, ZWLR_LAYER_SHELL_V1_LAYER_TOP,
		ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM,
		ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND
	};
	struct wlr_box full = { 0, 0, s->width, s->height }, usable = full,
	    scratch;
	struct mw_layer *l;
	int pass;
	unsigned i;

	for (pass = 0; pass < 2; pass++)
		for (i = 0; i < sizeof(order) / sizeof(order[0]); i++)
			wl_list_for_each(l, &s->layer_surfaces, link) {
				bool counts = l->wlr->surface->mapped &&
				    l->wlr->current.exclusive_zone > 0;

				if (!l->wlr->initialized ||
				    l->wlr->current.layer != order[i] ||
				    counts != (pass == 0))
					continue;
				scratch = usable;
				wlr_scene_layer_surface_v1_configure(l->scene,
				    &full, counts ? &usable : &scratch);
			}
	strip_area(s, &usable);
}

static void
layer_map(struct wl_listener *listener, void *data)
{
	struct mw_layer *l = wl_container_of(listener, l, map);

	layers_arrange(l->server);
	if (exclusive(l))
		give_keyboard(l);
}

static void
layer_unmap(struct wl_listener *listener, void *data)
{
	struct mw_layer *l = wl_container_of(listener, l, unmap);

	if (l->server->layer_focus == l)
		take_keyboard(l->server);
	layers_arrange(l->server);
}

static void
layer_commit(struct wl_listener *listener, void *data)
{
	struct mw_layer *l = wl_container_of(listener, l, commit);
	struct tinywl_server *s = l->server;

	if (!l->wlr->initialized)
		return;
	if (l->wlr->current.layer != l->layer) {
		l->layer = l->wlr->current.layer;
		wlr_scene_node_reparent(&l->scene->tree->node, s->layer[l->layer]);
	}
	if (l->wlr->initial_commit || l->wlr->current.committed != 0)
		layers_arrange(s);
}

/*
 * A popup of a layer surface: xdg-shell made it without a parent (and
 * meechowm.c gave it no tree), and layer-shell gives it this one.
 */
static void
layer_new_popup(struct wl_listener *listener, void *data)
{
	struct mw_layer *l = wl_container_of(listener, l, new_popup);
	struct wlr_xdg_popup *popup = data;

	popup->base->data = wlr_scene_xdg_surface_create(l->scene->tree,
	    popup->base);
}

static void
layer_destroy(struct wl_listener *listener, void *data)
{
	struct mw_layer *l = wl_container_of(listener, l, destroy);
	struct tinywl_server *s = l->server;

	if (s->layer_focus == l)
		take_keyboard(s);
	wl_list_remove(&l->map.link);
	wl_list_remove(&l->unmap.link);
	wl_list_remove(&l->commit.link);
	wl_list_remove(&l->destroy.link);
	wl_list_remove(&l->new_popup.link);
	wl_list_remove(&l->link);
	free(l);
	layers_arrange(s);
}

static void
new_layer_surface(struct wl_listener *listener, void *data)
{
	struct tinywl_server *s =
	    wl_container_of(listener, s, new_layer_surface);
	struct wlr_layer_surface_v1 *wlr = data;
	struct mw_layer *l;

	/* There is one screen; a surface that names none gets it. */
	if (wlr->output == NULL)
		wlr->output = s->output;
	if (wlr->output == NULL || (l = calloc(1, sizeof(*l))) == NULL) {
		wlr_layer_surface_v1_destroy(wlr);
		return;
	}
	l->server = s;
	l->wlr = wlr;
	l->layer = wlr->pending.layer;
	if ((l->scene = wlr_scene_layer_surface_v1_create(s->layer[l->layer],
	    wlr)) == NULL) {
		free(l);
		wlr_layer_surface_v1_destroy(wlr);
		return;
	}
	wlr->data = l;

	l->map.notify = layer_map;
	wl_signal_add(&wlr->surface->events.map, &l->map);
	l->unmap.notify = layer_unmap;
	wl_signal_add(&wlr->surface->events.unmap, &l->unmap);
	l->commit.notify = layer_commit;
	wl_signal_add(&wlr->surface->events.commit, &l->commit);
	l->destroy.notify = layer_destroy;
	wl_signal_add(&wlr->events.destroy, &l->destroy);
	l->new_popup.notify = layer_new_popup;
	wl_signal_add(&wlr->events.new_popup, &l->new_popup);
	wl_list_insert(&s->layer_surfaces, &l->link);
	wlr_log(WLR_DEBUG, "layer surface %s on layer %d",
	    wlr->namespace != NULL ? wlr->namespace : "-", (int)l->layer);
}

void
layers_init(struct tinywl_server *s)
{
	wl_list_init(&s->layer_surfaces);
	if ((s->layer_shell = wlr_layer_shell_v1_create(s->wl_display, 4)) ==
	    NULL) {
		wlr_log(WLR_ERROR, "no layer shell");
		return;
	}
	s->new_layer_surface.notify = new_layer_surface;
	wl_signal_add(&s->layer_shell->events.new_surface,
	    &s->new_layer_surface);
}
