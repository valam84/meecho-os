/*
 * The on-screen keyboard's half in the compositor (stage 11.9): three
 * protocols, and when the keyboard is up.
 *
 *   virtual-keyboard-v1  a client's keyboard (meechowm-osk's): its keys go
 *                        into the seat as a USB keyboard's do, to the
 *                        window with the focus - but past the
 *                        compositor's own keys, so that Alt and Esc on the
 *                        screen do not end the desktop
 *   text-input-v3        a window's text field (foot has one): told when
 *                        it has the keyboard focus, it says when it wants
 *                        text - enable - and when no more
 *   input-method-v2      the keyboard client: activated while a text
 *                        field wants text, deactivated after; what it
 *                        commits (text, preedit, deletions) goes to that
 *                        field.  One per seat; a second is told
 *                        "unavailable".
 *
 * The keyboard is up - its input method active - while the focused window
 * has a text field enabled, or when it is asked for: "osk on" (Super+t,
 * the palette) puts it up whatever has the focus, "osk off" takes it
 * down, "osk" turns it over, "osk auto" leaves it to the text fields
 * again; and the next field that wants text does too.  With no keyboard
 * client connected, asking for it starts one (meechowm-osk).
 */
#include <sys/types.h>

#include <stdlib.h>
#include <string.h>

#include <wayland-server-core.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_input_method_v2.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_text_input_v3.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/util/log.h>

#include "wm.h"

enum { OSK_AUTO, OSK_ON, OSK_OFF };

/* A window's text field. */
struct mw_text_input {
	struct wl_list link;			/* ti.inputs */
	struct wlr_text_input_v3 *wlr;
	struct wl_listener enable;
	struct wl_listener commit;
	struct wl_listener disable;
	struct wl_listener destroy;
};

static struct {
	struct tinywl_server *server;
	struct wlr_virtual_keyboard_manager_v1 *vk_mgr;
	struct wlr_text_input_manager_v3 *ti_mgr;
	struct wlr_input_method_manager_v2 *im_mgr;
	struct wl_listener new_virtual_keyboard;
	struct wl_listener new_text_input;
	struct wl_listener new_input_method;
	struct wl_listener focus_change;	/* the seat's keyboard focus */
	struct wl_list inputs;			/* mw_text_input.link */

	struct wlr_input_method_v2 *im;		/* the keyboard client's */
	struct wl_listener im_commit;
	struct wl_listener im_destroy;
	bool told;				/* im activated */
	int mode;				/* OSK_ */
} ti;

/* The text field that has the keyboard and wants text, or NULL. */
static struct mw_text_input *
active(void)
{
	struct wlr_surface *focus =
	    ti.server->seat->keyboard_state.focused_surface;
	struct mw_text_input *t;

	if (focus == NULL)
		return NULL;
	wl_list_for_each(t, &ti.inputs, link)
		if (t->wlr->focused_surface == focus && t->wlr->current_enabled)
			return t;
	return NULL;
}

static bool
wanted(void)
{
	return ti.mode == OSK_ON || (ti.mode == OSK_AUTO && active() != NULL);
}

/* What the field says about itself, to the input method. */
static void
tell_field(struct mw_text_input *t)
{
	struct wlr_text_input_v3_state *c = &t->wlr->current;

	if (t->wlr->active_features & WLR_TEXT_INPUT_V3_FEATURE_SURROUNDING_TEXT)
		wlr_input_method_v2_send_surrounding_text(ti.im,
		    c->surrounding.text != NULL ? c->surrounding.text : "",
		    c->surrounding.cursor, c->surrounding.anchor);
	wlr_input_method_v2_send_text_change_cause(ti.im, c->text_change_cause);
	if (t->wlr->active_features & WLR_TEXT_INPUT_V3_FEATURE_CONTENT_TYPE)
		wlr_input_method_v2_send_content_type(ti.im,
		    c->content_type.hint, c->content_type.purpose);
}

/* The input method told whether the keyboard is to be up. */
static void
update(void)
{
	struct mw_text_input *t;
	bool want = wanted();

	if (ti.im == NULL || want == ti.told)
		return;
	ti.told = want;
	if (want) {
		wlr_input_method_v2_send_activate(ti.im);
		if ((t = active()) != NULL)
			tell_field(t);
	} else
		wlr_input_method_v2_send_deactivate(ti.im);
	wlr_input_method_v2_send_done(ti.im);
}

static bool
same_client(struct wlr_text_input_v3 *t, struct wlr_surface *s)
{
	return s != NULL && wl_resource_get_client(t->resource) ==
	    wl_resource_get_client(s->resource);
}

/*===========================================================================*
 *    text fields                                                            *
 *===========================================================================*/
static void
ti_enable(struct wl_listener *listener, void *data)
{
	/* A field that wants text has the say again. */
	ti.mode = OSK_AUTO;
	update();
}

static void
ti_commit(struct wl_listener *listener, void *data)
{
	struct mw_text_input *t = wl_container_of(listener, t, commit);

	if (ti.im != NULL && ti.told && active() == t) {
		tell_field(t);
		wlr_input_method_v2_send_done(ti.im);
	}
}

static void
ti_disable(struct wl_listener *listener, void *data)
{
	update();
}

static void
ti_destroy(struct wl_listener *listener, void *data)
{
	struct mw_text_input *t = wl_container_of(listener, t, destroy);

	wl_list_remove(&t->enable.link);
	wl_list_remove(&t->commit.link);
	wl_list_remove(&t->disable.link);
	wl_list_remove(&t->destroy.link);
	wl_list_remove(&t->link);
	free(t);
	update();
}

static void
new_text_input(struct wl_listener *listener, void *data)
{
	struct wlr_text_input_v3 *wlr = data;
	struct wlr_surface *focus =
	    ti.server->seat->keyboard_state.focused_surface;
	struct mw_text_input *t;

	if ((t = calloc(1, sizeof(*t))) == NULL)
		return;
	t->wlr = wlr;
	t->enable.notify = ti_enable;
	wl_signal_add(&wlr->events.enable, &t->enable);
	t->commit.notify = ti_commit;
	wl_signal_add(&wlr->events.commit, &t->commit);
	t->disable.notify = ti_disable;
	wl_signal_add(&wlr->events.disable, &t->disable);
	t->destroy.notify = ti_destroy;
	wl_signal_add(&wlr->events.destroy, &t->destroy);
	wl_list_insert(&ti.inputs, &t->link);
	/* Its window may have the focus already. */
	if (same_client(wlr, focus))
		wlr_text_input_v3_send_enter(wlr, focus);
}

/* The keyboard went to another surface: the fields follow it. */
static void
focus_change(struct wl_listener *listener, void *data)
{
	struct wlr_seat_keyboard_focus_change_event *e = data;
	struct mw_text_input *t;

	wl_list_for_each(t, &ti.inputs, link) {
		if (t->wlr->focused_surface != NULL &&
		    t->wlr->focused_surface != e->new_surface)
			wlr_text_input_v3_send_leave(t->wlr);
		if (t->wlr->focused_surface == NULL &&
		    same_client(t->wlr, e->new_surface))
			wlr_text_input_v3_send_enter(t->wlr, e->new_surface);
	}
	update();
}

/*===========================================================================*
 *    the input method                                                       *
 *===========================================================================*/
/* What the keyboard client typed as text, to the field. */
static void
im_commit(struct wl_listener *listener, void *data)
{
	struct wlr_input_method_v2_state *c = &ti.im->current;
	struct mw_text_input *t = active();

	if (t == NULL)
		return;
	if (c->preedit.text != NULL)
		wlr_text_input_v3_send_preedit_string(t->wlr, c->preedit.text,
		    c->preedit.cursor_begin, c->preedit.cursor_end);
	if (c->commit_text != NULL)
		wlr_text_input_v3_send_commit_string(t->wlr, c->commit_text);
	if (c->delete.before_length != 0 || c->delete.after_length != 0)
		wlr_text_input_v3_send_delete_surrounding_text(t->wlr,
		    c->delete.before_length, c->delete.after_length);
	wlr_text_input_v3_send_done(t->wlr);
}

static void
im_destroy(struct wl_listener *listener, void *data)
{
	wl_list_remove(&ti.im_commit.link);
	wl_list_remove(&ti.im_destroy.link);
	ti.im = NULL;
	ti.told = false;
}

static void
new_input_method(struct wl_listener *listener, void *data)
{
	struct wlr_input_method_v2 *im = data;

	if (ti.im != NULL) {
		wlr_input_method_v2_send_unavailable(im);
		return;
	}
	ti.im = im;
	ti.told = false;
	ti.im_commit.notify = im_commit;
	wl_signal_add(&im->events.commit, &ti.im_commit);
	ti.im_destroy.notify = im_destroy;
	wl_signal_add(&im->events.destroy, &ti.im_destroy);
	update();
}

/*===========================================================================*
 *    the virtual keyboard                                                   *
 *===========================================================================*/
static void
new_virtual_keyboard(struct wl_listener *listener, void *data)
{
	struct wlr_virtual_keyboard_v1 *vk = data;

	/* A keyboard like any other (meechowm.c); the client gives it its
	 * keymap. */
	wm_add_input(ti.server, &vk->keyboard.base);
}

bool
textinput_virtual(struct wlr_keyboard *kb)
{
	return wlr_input_device_get_virtual_keyboard(&kb->base) != NULL;
}

/*===========================================================================*
 *    commands                                                               *
 *===========================================================================*/
/* "osk [on|off|auto]": the keyboard up or down; with nothing, turned over. */
bool
textinput_command(struct tinywl_server *s, const char *arg,
	struct mw_reply *r)
{
	if (arg[0] == '\0')
		ti.mode = ti.told ? OSK_OFF : OSK_ON;
	else if (strcmp(arg, "on") == 0)
		ti.mode = OSK_ON;
	else if (strcmp(arg, "off") == 0)
		ti.mode = OSK_OFF;
	else if (strcmp(arg, "auto") == 0)
		ti.mode = OSK_AUTO;
	else {
		r->error = true;
		wm_reply(r, "error: osk %s: on, off, auto or nothing\n", arg);
		return false;
	}
	if (ti.im == NULL && wanted())
		wm_spawn("meechowm-osk");
	update();
	return true;
}

void
textinput_state(struct tinywl_server *s, struct mw_reply *r)
{
	static const char *const mode[] = { "auto", "on", "off" };
	struct mw_text_input *t = active();
	pid_t pid = 0;
	uid_t uid;
	gid_t gid;

	if (t != NULL)
		wl_client_get_credentials(wl_resource_get_client(
		    t->wlr->resource), &pid, &uid, &gid);
	wm_reply(r, "osk %s mode %s method %d fields %d active %d\n",
	    ti.told ? "up" : "down", mode[ti.mode], ti.im != NULL ? 1 : 0,
	    wl_list_length(&ti.inputs), (int)pid);
}

void
textinput_init(struct tinywl_server *s)
{
	ti.server = s;
	wl_list_init(&ti.inputs);
	ti.mode = OSK_AUTO;
	ti.vk_mgr = wlr_virtual_keyboard_manager_v1_create(s->wl_display);
	ti.ti_mgr = wlr_text_input_manager_v3_create(s->wl_display);
	ti.im_mgr = wlr_input_method_manager_v2_create(s->wl_display);
	if (ti.vk_mgr == NULL || ti.ti_mgr == NULL || ti.im_mgr == NULL) {
		wlr_log(WLR_ERROR, "no on-screen keyboard protocols");
		ti.server = NULL;
		return;
	}
	ti.new_virtual_keyboard.notify = new_virtual_keyboard;
	wl_signal_add(&ti.vk_mgr->events.new_virtual_keyboard,
	    &ti.new_virtual_keyboard);
	ti.new_text_input.notify = new_text_input;
	wl_signal_add(&ti.ti_mgr->events.new_text_input, &ti.new_text_input);
	ti.new_input_method.notify = new_input_method;
	wl_signal_add(&ti.im_mgr->events.new_input_method,
	    &ti.new_input_method);
	ti.focus_change.notify = focus_change;
	wl_signal_add(&s->seat->keyboard_state.events.focus_change,
	    &ti.focus_change);
}

/*
 * After the clients are gone, before the display: wlroots asserts that a
 * global's signals have no listeners left when it is destroyed.
 */
void
textinput_fini(void)
{
	if (ti.server == NULL)
		return;
	wl_list_remove(&ti.new_virtual_keyboard.link);
	wl_list_remove(&ti.new_text_input.link);
	wl_list_remove(&ti.new_input_method.link);
	wl_list_remove(&ti.focus_change.link);
	if (ti.im != NULL)
		im_destroy(NULL, NULL);
	ti.server = NULL;
}
