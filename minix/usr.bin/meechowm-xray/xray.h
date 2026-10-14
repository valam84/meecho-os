/*
 * meechowm-xray: the desktop's x-ray view (stage 11.7).  The windows go
 * back, and around them stand the processes they talk to - VFS, MFS, the
 * network stack, the drivers, the kernel - joined by lines as thick as the
 * stream of messages between them.  A restarted service flashes; a click
 * on a window asks the compositor for its passport.
 *
 * Where it comes from:
 *
 *   meechowm -m state      the windows: where they are, and their pids,
 *                          which the compositor has from the credentials
 *                          of each client's socket (uds, LOCAL_PEEREID)
 *   /proc/<pid>/ipc        each process's row of the kernel's message
 *                          matrix (<minix/ipcflow.h>): what it has sent,
 *                          to whom
 *   /proc/<pid>/psinfo     which processes are the system's
 *   /proc/service/<label>  the services' labels, and their restarts
 *
 *   model.c   reads all of that, a sample at a time, into a graph
 *   draw.c    lays the graph out and draws it (pixman, fcft)
 *   xray.c    the layer surface, the pointer and keys; and -d, the same
 *             graph as text, which the stand (port/gfx/test/qemu-xray.py)
 *             checks against /proc/ktrace
 */
#ifndef XRAY_H
#define XRAY_H

#include <sys/types.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define XR_MAXWIN	32
#define XR_MAXNODE	128
#define XR_MAXEDGE	1024
#define XR_MAXDEPTH	3	/* hops from a window that a service may be */

enum xr_kind {
	XR_WINDOW,	/* a client of the compositor with a window shown */
	XR_DESK,	/* the compositor itself */
	XR_SERVICE,	/* a system process */
	XR_KERNEL,	/* the kernel's tasks, as one: HARDWARE, CLOCK,
			 * SYSTEM, ASYNCM */
	XR_OTHER	/* a user process that is not a window: read, not shown */
};

struct xr_win {
	pid_t pid;
	int x, y, w, h;		/* on the screen, where it is drawn now */
	int dx, dy, dw, dh;	/* where the x-ray draws it, gone back */
	bool focus;
	char title[64];
	int node;		/* its process's node */
};

struct xr_node {
	enum xr_kind kind;
	int32_t ep;		/* endpoint; the kernel node: KERNEL */
	pid_t pid;
	int slot;		/* in the process table, as /proc/ktrace has it */
	char name[16];		/* the kernel's name for it */
	char label[32];		/* the service's label, else the name */
	char type;		/* psinfo: 'T' task, 'S' system, 'U' user */
	uid_t uid;
	unsigned long mem;	/* bytes, psinfo */
	uint64_t kernel;	/* of its row, what the kernel sent in its name */
	uint64_t sent;		/* its row's sum */
	double rate;		/* messages a second, in and out, smoothed */
	int depth;		/* hops from a window or the compositor */
	bool shown;
	unsigned flash_ms;	/* a restart's flash, left to show */
	int x, y, w, h;		/* laid out (draw.c) */
};

struct xr_edge {
	int from, to;		/* nodes: from sends to to */
	uint64_t count;		/* all it has sent since either began */
	double rate;		/* a second, smoothed */
	bool shown;
};

struct xr_model {
	int sw, sh;		/* the screen */
	int ax, ay, aw, ah;	/* what windows may take of it */
	pid_t desk_pid;		/* the compositor, from its socket's peer */
	struct xr_win win[XR_MAXWIN];
	int nwin;
	struct xr_node node[XR_MAXNODE];
	int nnode;
	struct xr_edge edge[XR_MAXEDGE];
	int nedge;
	double rate;		/* all shown edges, a second */
	unsigned samples;
};

/* model.c */
const char *xr_sock_path(void);
int xr_command(const char *line, char *reply, size_t size);
bool xr_sample(struct xr_model *m, unsigned elapsed_ms);
void xr_tick(struct xr_model *m, unsigned elapsed_ms);
bool xr_flashing(const struct xr_model *m);
void xr_print_services(FILE *f);

/* draw.c */
struct xr_draw;
struct xr_draw *xr_draw_new(void);
void xr_layout(struct xr_draw *d, struct xr_model *m, int width, int height);
void xr_render(struct xr_draw *d, const struct xr_model *m, uint32_t *px,
	int width, int height, int stride);
int xr_hit_window(const struct xr_draw *d, const struct xr_model *m, int x,
	int y);
double xr_line_width(double rate);

#endif
