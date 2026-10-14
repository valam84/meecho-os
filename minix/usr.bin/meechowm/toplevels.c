/*
 * The windows, told to clients that list them (stage 11.7):
 * wlr-foreign-toplevel-management, the protocol of task bars and window
 * switchers - yambar's foreign-toplevel module on the panel among them.
 *
 * A handle per mapped window: its title and app id as the client sets
 * them, the screen it is on, and whether it is the window with the
 * keyboard.  What a client asks of a handle is done by the strip's own
 * means: activate - strip_focus(), which goes to the window's workspace
 * if it is another; close - the client is asked to close, as Mod+q does;
 * maximize - all of the screen's width, as "maximize" does.  Minimize and
 * fullscreen have nothing in the strip to be done by; the handle says the
 * window is neither.
 */
#include <stdlib.h>

#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>

#include "layout.h"
#include "wm.h"

static struct wlr_foreign_toplevel_manager_v1 *manager;
static struct wl_listener focus_change;
static struct tinywl_server *server;

/* The window with the keyboard, active; every other one, not. */
static void
handle_focus_change(struct wl_listener *listener, void *data)
{
	struct wlr_seat_keyboard_focus_change_event *e = data;
	struct tinywl_toplevel *t;

	(void)listener;
	wl_list_for_each(t, &server->toplevels, link)
		if (t->ftl != NULL)
			wlr_foreign_toplevel_handle_v1_set_activated(t->ftl,
			    e->new_surface != NULL &&
			    e->new_surface == t->xdg_toplevel->base->surface);
}

void
toplevels_init(struct tinywl_server *s)
{
	server = s;
	if ((manager = wlr_foreign_toplevel_manager_v1_create(s->wl_display)) ==
	    NULL) {
		wlr_log(WLR_ERROR, "no wlr-foreign-toplevel-management");
		return;
	}
	focus_change.notify = handle_focus_change;
	wl_signal_add(&s->seat->keyboard_state.events.focus_change,
	    &focus_change);
}

void
toplevels_fini(void)
{
	if (manager != NULL)
		wl_list_remove(&focus_change.link);
}

static void
handle_activate(struct wl_listener *listener, void *data)
{
	struct tinywl_toplevel *t = wl_container_of(listener, t, ftl_activate);

	(void)data;
	wlr_log(WLR_DEBUG, "toplevels: activate %s",
	    t->xdg_toplevel->title != NULL ? t->xdg_toplevel->title : "-");
	strip_focus(t);
	wm_pointer_refresh(t->server);
}

static void
handle_close(struct wl_listener *listener, void *data)
{
	struct tinywl_toplevel *t = wl_container_of(listener, t, ftl_close);

	(void)data;
	wlr_log(WLR_DEBUG, "toplevels: close %s",
	    t->xdg_toplevel->title != NULL ? t->xdg_toplevel->title : "-");
	wlr_xdg_toplevel_send_close(t->xdg_toplevel);
}

static void
handle_maximize(struct wl_listener *listener, void *data)
{
	struct tinywl_toplevel *t = wl_container_of(listener, t, ftl_maximize);
	struct wlr_foreign_toplevel_handle_v1_maximized_event *e = data;

	if (e->maximized != (t->width == MC_W_FULL)) {
		strip_maximize(t);
		wm_pointer_refresh(t->server);
	}
	wlr_foreign_toplevel_handle_v1_set_maximized(t->ftl,
	    t->width == MC_W_FULL);
}

static void
handle_title(struct wl_listener *listener, void *data)
{
	struct tinywl_toplevel *t = wl_container_of(listener, t, ftl_title);

	(void)data;
	if (t->ftl != NULL && t->xdg_toplevel->title != NULL)
		wlr_foreign_toplevel_handle_v1_set_title(t->ftl,
		    t->xdg_toplevel->title);
}

static void
handle_app_id(struct wl_listener *listener, void *data)
{
	struct tinywl_toplevel *t = wl_container_of(listener, t, ftl_app_id);

	(void)data;
	if (t->ftl != NULL && t->xdg_toplevel->app_id != NULL)
		wlr_foreign_toplevel_handle_v1_set_app_id(t->ftl,
		    t->xdg_toplevel->app_id);
}

/* A window on the screen: a handle for it. */
void
toplevels_map(struct tinywl_toplevel *t)
{
	struct wlr_xdg_toplevel *x = t->xdg_toplevel;
	struct wlr_surface *focused;

	if (manager == NULL || t->ftl != NULL ||
	    (t->ftl = wlr_foreign_toplevel_handle_v1_create(manager)) == NULL)
		return;
	if (x->title != NULL)
		wlr_foreign_toplevel_handle_v1_set_title(t->ftl, x->title);
	if (x->app_id != NULL)
		wlr_foreign_toplevel_handle_v1_set_app_id(t->ftl, x->app_id);
	if (t->server->output != NULL)
		wlr_foreign_toplevel_handle_v1_output_enter(t->ftl,
		    t->server->output);
	wlr_foreign_toplevel_handle_v1_set_maximized(t->ftl,
	    t->width == MC_W_FULL);
	focused = t->server->seat->keyboard_state.focused_surface;
	wlr_foreign_toplevel_handle_v1_set_activated(t->ftl,
	    focused != NULL && focused == x->base->surface);

	t->ftl_activate.notify = handle_activate;
	wl_signal_add(&t->ftl->events.request_activate, &t->ftl_activate);
	t->ftl_close.notify = handle_close;
	wl_signal_add(&t->ftl->events.request_close, &t->ftl_close);
	t->ftl_maximize.notify = handle_maximize;
	wl_signal_add(&t->ftl->events.request_maximize, &t->ftl_maximize);
	t->ftl_title.notify = handle_title;
	wl_signal_add(&x->events.set_title, &t->ftl_title);
	t->ftl_app_id.notify = handle_app_id;
	wl_signal_add(&x->events.set_app_id, &t->ftl_app_id);
}

/* Off the screen: the handle goes, and clients are told. */
void
toplevels_unmap(struct tinywl_toplevel *t)
{
	if (t->ftl == NULL)
		return;
	wl_list_remove(&t->ftl_activate.link);
	wl_list_remove(&t->ftl_close.link);
	wl_list_remove(&t->ftl_maximize.link);
	wl_list_remove(&t->ftl_title.link);
	wl_list_remove(&t->ftl_app_id.link);
	wlr_foreign_toplevel_handle_v1_destroy(t->ftl);
	t->ftl = NULL;
}

/* The width changed by other means than the handle: say so. */
void
toplevels_width(struct tinywl_toplevel *t)
{
	if (t->ftl != NULL)
		wlr_foreign_toplevel_handle_v1_set_maximized(t->ftl,
		    t->width == MC_W_FULL);
}
