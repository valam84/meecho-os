/*
 * meechowm's own structures, shared by the three parts of the compositor
 * that are not glue to MEECHO's devices (meechowm.h has those):
 *
 *   meechowm.c   tinywl's code: the protocol, the keyboard, the mouse,
 *                and the keys that are the compositor's
 *   strip.c      the strip: workspaces, columns, focus, and the commands
 *                that act on them
 *   layers.c     layer surfaces: panels and launchers over and under it
 *   ipc.c        the commands over a socket (meechowm -m)
 */
#ifndef MEECHOWM_WM_H
#define MEECHOWM_WM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <wayland-server-core.h>
#include <wlr/util/box.h>

struct mc_fb;
struct mw_workspace;
struct mw_decoration;
struct mw_layer;
struct wlr_surface;

struct tinywl_server {
	struct mc_fb *fb;		/* the screen; NULL: headless */
	struct wl_display *wl_display;
	struct wlr_backend *backend;
	struct wlr_renderer *renderer;
	struct wlr_allocator *allocator;
	struct wlr_scene *scene;
	struct wlr_scene_output_layout *scene_layout;

	struct wlr_xdg_shell *xdg_shell;
	struct wl_listener new_xdg_toplevel;
	struct wl_listener new_xdg_popup;
	struct wl_list toplevels;

	struct wlr_xdg_decoration_manager_v1 *decoration_mgr;
	struct wl_listener new_decoration;

	struct wlr_cursor *cursor;
	struct wlr_xcursor_manager *cursor_mgr;
	struct wl_listener cursor_motion;
	struct wl_listener cursor_motion_absolute;
	struct wl_listener cursor_button;
	struct wl_listener cursor_axis;
	struct wl_listener cursor_frame;

	struct wlr_seat *seat;
	struct wl_listener new_input;
	struct wl_listener request_cursor;
	struct wl_listener pointer_focus_change;
	struct wl_listener request_set_selection;
	struct wl_list keyboards;

	struct wlr_output_layout *output_layout;
	struct wl_list outputs;
	struct wl_listener new_output;
	struct wlr_output *output;	/* the one screen, once it is there */

	/* Layer surfaces (layers.c): panels, launchers, backgrounds. */
	struct wlr_layer_shell_v1 *layer_shell;
	struct wl_listener new_layer_surface;
	struct wlr_scene_tree *layer[4];	/* background, bottom, top,
						 * overlay: the first two under
						 * the strip, the others over it */
	struct wl_list layer_surfaces;	/* mw_layer.link */
	struct mw_layer *layer_focus;	/* has the keyboard, or NULL */
	const char *launcher;		/* what Mod+d starts */

	/* The strip (strip.c). */
	int width, height;		/* the screen */
	struct wlr_box area;		/* what columns may take of it */
	int gap;			/* between columns, and at the edges */
	int default_width;		/* a new column's (layout.h) */
	uint32_t mod;			/* the compositor's modifier key */
	const char *terminal;		/* what Mod+Return starts */
	struct wlr_scene_rect *background;
	struct wlr_scene_tree *ring;	/* around the focused column */
	struct wlr_scene_rect *ring_side[4];
	struct wlr_scene_tree *layer_ws;	/* the workspaces' trees */
	struct wlr_scene_tree *hidden;	/* windows not mapped: never drawn */
	struct wl_list workspaces;	/* mw_workspace.link, top to bottom */
	struct mw_workspace *ws;	/* the one on the screen */

	/*
	 * Motion (strip.c): everything that moves - columns along the strip,
	 * workspaces up and down - goes from where it is drawn to where the
	 * layout puts it, on one clock, restarted by every change.
	 */
	int anim_ms;			/* how long a move takes; 0: none */
	uint32_t anim_start;		/* the clock's start, in ms */
	bool animating;
	struct wl_event_source *anim_timer;
};

struct tinywl_toplevel {
	struct wl_list link;
	struct tinywl_server *server;
	struct wlr_xdg_toplevel *xdg_toplevel;
	struct wlr_scene_tree *scene_tree;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener commit;
	struct wl_listener destroy;
	struct wl_listener request_maximize;
	struct wl_listener request_fullscreen;

	/* Its column (strip.c). */
	struct mw_workspace *ws;	/* NULL while it is not mapped */
	struct wl_list column_link;	/* mw_workspace.columns */
	int width;			/* a preset of layout.h */
	int width_was;			/* the one before "maximize" */
	struct wlr_box box;		/* the column on the screen */
	int at_x;			/* where it is drawn: box.x, or on the way */
	int from_x;			/* where the move started */
	bool shown;			/* drawn before: it moves, not jumps */
	int cfg_width, cfg_height;	/* the size last asked of it */
	struct mw_decoration *decoration;
};

/* A workspace: a strip of columns. */
struct mw_workspace {
	struct wl_list link;		/* tinywl_server.workspaces */
	struct tinywl_server *server;
	struct wlr_scene_tree *tree;	/* its columns' windows */
	struct wl_list columns;		/* tinywl_toplevel.column_link */
	struct tinywl_toplevel *focus;	/* NULL when it is empty */
	int view;			/* layout.h */
	int at_y, from_y, to_y;		/* its tree's y: 0 on the screen,
					 * the screen's height above or below */
};

/* What a command answers: text, and whether it failed. */
struct mw_reply {
	char buf[16384];
	size_t len;
	bool error;
};

/* meechowm.c */
void focus_toplevel(struct tinywl_toplevel *toplevel);
void wm_pointer_refresh(struct tinywl_server *server);

/* layers.c */
void layers_init(struct tinywl_server *server);
void layers_arrange(struct tinywl_server *server);
bool layers_hold_keyboard(struct tinywl_server *server);
void layers_click(struct tinywl_server *server, struct wlr_surface *surface);

/* strip.c */
void strip_init(struct tinywl_server *server);
void strip_screen(struct tinywl_server *server, int width, int height);
void strip_area(struct tinywl_server *server, const struct wlr_box *area);
void strip_initial_commit(struct tinywl_toplevel *toplevel);
void strip_map(struct tinywl_toplevel *toplevel);
void strip_unmap(struct tinywl_toplevel *toplevel);
void strip_commit(struct tinywl_toplevel *toplevel);
void strip_focus(struct tinywl_toplevel *toplevel);
void strip_maximize(struct tinywl_toplevel *toplevel);
void wm_command(struct tinywl_server *server, const char *line,
	struct mw_reply *reply);
void wm_reply(struct mw_reply *reply, const char *fmt, ...)
	__attribute__((__format__(__printf__, 2, 3)));
void wm_spawn(const char *cmd);

/* ipc.c */
bool ipc_start(struct tinywl_server *server);
void ipc_stop(void);
int ipc_send(int argc, char **argv);

#endif
