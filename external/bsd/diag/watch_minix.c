/*
 * MEECHO: the event loop of diag-router (dist/router/watch.c's interface,
 * watch.h) over select(2) alone.  The original drives its read and write
 * queues with Linux's aio (io_submit() completing into an eventfd), which
 * this system does not have; here a queue's head is read or written when
 * select() says its descriptor is ready, and the completion called at
 * once - the same order of events, one buffer at a time per queue, as the
 * original keeps it.  Timers, flow control and quit callbacks are the
 * original's logic.
 */

#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>

#include <err.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "list.h"
#include "mbuf.h"
#include "util.h"
#include "watch.h"

#define FLOW_WATERMARK	10

struct watch_flow {
	int packets;
};

struct watch {
	int fd;
	int (*cb)(int, void *);
	void *data;
	struct list_head *queue;
	bool is_write;
	struct watch_flow *flow;
	int (*complete)(struct mbuf *, void *);
	struct list_head node;
};

struct timer {
	void (*cb)(void *);
	void *data;
	unsigned int interval;
	bool repeat;
	struct timeval tick;
	struct list_head node;
};

static struct list_head timers = LIST_INIT(timers);
static struct list_head read_watches = LIST_INIT(read_watches);
static struct list_head queue_watches = LIST_INIT(queue_watches);
static struct list_head quit_watches = LIST_INIT(quit_watches);
static bool do_watch_quit;

struct watch_flow *
watch_flow_new(void)
{
	return calloc(1, sizeof(struct watch_flow));
}

void
watch_flow_inc(struct watch_flow *flow)
{
	if (flow)
		flow->packets++;
}

static void
watch_flow_dec(struct watch_flow *flow)
{
	if (!flow)
		return;
	if (!flow->packets)
		fprintf(stderr, "unbalanced flow control\n");
	else
		flow->packets--;
}

static bool
watch_flow_blocked(struct watch_flow *flow)
{
	return flow && flow->packets > FLOW_WATERMARK;
}

static struct watch *
watch_new(void)
{
	struct watch *w = calloc(1, sizeof(*w));

	if (!w)
		err(1, "calloc");
	return w;
}

int
watch_add_readfd(int fd, int (*cb)(int, void *), void *data,
	struct watch_flow *flow)
{
	struct watch *w = watch_new();

	w->fd = fd;
	w->cb = cb;
	w->data = data;
	w->flow = flow;
	list_add(&read_watches, &w->node);
	return 0;
}

int
watch_add_readq(int fd, struct list_head *queue,
	int (*cb)(struct mbuf *mbuf, void *data), void *data)
{
	struct watch *w = watch_new();

	w->fd = fd;
	w->complete = cb;
	w->data = data;
	w->queue = queue;
	w->is_write = false;
	list_add(&queue_watches, &w->node);
	return 0;
}

static int
watch_free_write(struct mbuf *mbuf, void *data)
{
	(void)data;
	watch_flow_dec(mbuf->flow);
	free(mbuf);
	return 0;
}

int
watch_add_writeq(int fd, struct list_head *queue)
{
	struct watch *w = watch_new();

	w->fd = fd;
	w->queue = queue;
	w->data = w;
	w->complete = watch_free_write;
	w->is_write = true;
	list_add(&queue_watches, &w->node);
	return 0;
}

static void
remove_from(struct list_head *list, int fd)
{
	struct list_head *item, *next;
	struct watch *w;

	list_for_each_safe(item, next, list) {
		w = container_of(item, struct watch, node);
		if (w->fd == fd) {
			list_del(&w->node);
			free(w);
		}
	}
}

void
watch_remove_fd(int fd)
{
	remove_from(&read_watches, fd);
	remove_from(&queue_watches, fd);
}

void
watch_remove_writeq(int fd)
{
	remove_from(&queue_watches, fd);
}

int
watch_add_quit(int (*cb)(int, void *), void *data)
{
	struct watch *w = watch_new();

	w->cb = cb;
	w->data = data;
	list_add(&quit_watches, &w->node);
	return 0;
}

static void
watch_set_timer(struct timer *timer)
{
	struct timeval now, tv;

	if (gettimeofday(&now, NULL) < 0)
		err(1, "failed to gettimeofday");
	tv.tv_sec = timer->interval / 1000;
	tv.tv_usec = (timer->interval % 1000) * 1000;
	timeradd(&now, &tv, &timer->tick);
}

int
watch_add_timer(void (*cb)(void *), void *data, unsigned int interval,
	bool repeat)
{
	struct timer *t = calloc(1, sizeof(*t));

	if (!t)
		err(1, "calloc");
	t->cb = cb;
	t->data = data;
	t->interval = interval;
	t->repeat = repeat;
	list_add(&timers, &t->node);
	watch_set_timer(t);
	return 0;
}

static struct timer *
watch_get_next_timer(void)
{
	struct timer *selected, *timer;

	if (list_empty(&timers))
		return NULL;
	selected = container_of(timers.next, struct timer, node);
	list_for_each_entry(timer, &timers, node)
		if (timercmp(&timer->tick, &selected->tick, <))
			selected = timer;
	return selected;
}

void
watch_quit(void)
{
	do_watch_quit = true;
}

/* A queue's head, read or written now that its descriptor is ready. */
static void
watch_do_queue(struct watch *w)
{
	struct mbuf *mbuf;
	ssize_t n;

	if (list_empty(w->queue))
		return;
	mbuf = list_entry_first(w->queue, struct mbuf, node);
	if (w->is_write)
		n = write(w->fd, mbuf->data, mbuf->size);
	else
		n = read(w->fd, mbuf->data, mbuf->size);
	if (n < 0 && (errno == EAGAIN || errno == EINTR))
		return;
	list_del(&mbuf->node);
	if (!w->is_write && n >= 0)
		mbuf->offset = n;
	w->complete(mbuf, w->data);
}

void
watch_run(void)
{
	struct timeval *timeout, now, tv;
	struct timer *timer;
	struct watch *w, *next;
	fd_set rfds, wfds;
	int nfds, ret;

	while (!do_watch_quit) {
		FD_ZERO(&rfds);
		FD_ZERO(&wfds);
		nfds = 0;

		list_for_each_entry(w, &read_watches, node) {
			if (watch_flow_blocked(w->flow))
				continue;
			FD_SET(w->fd, &rfds);
			nfds = MAX(w->fd + 1, nfds);
		}
		list_for_each_entry(w, &queue_watches, node) {
			if (list_empty(w->queue))
				continue;
			FD_SET(w->fd, w->is_write ? &wfds : &rfds);
			nfds = MAX(w->fd + 1, nfds);
		}

		timer = watch_get_next_timer();
		if (timer) {
			gettimeofday(&now, NULL);
			timersub(&timer->tick, &now, &tv);
			if (tv.tv_sec < 0)
				tv.tv_sec = tv.tv_usec = 0;
			timeout = &tv;
		} else
			timeout = NULL;

		ret = select(nfds, &rfds, &wfds, NULL, timeout);
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			warn("failed to select");
			break;
		}

		if (ret == 0 && timer) {
			timer->cb(timer->data);
			if (timer->repeat)
				watch_set_timer(timer);
			else {
				list_del(&timer->node);
				free(timer);
			}
		}

		list_for_each_entry_safe(w, next, &queue_watches, node)
			if (FD_ISSET(w->fd, w->is_write ? &wfds : &rfds))
				watch_do_queue(w);

		list_for_each_entry_safe(w, next, &read_watches, node)
			if (FD_ISSET(w->fd, &rfds) && w->cb(w->fd, w->data) < 0)
				list_del(&w->node);
	}

	list_for_each_entry(w, &quit_watches, node)
		w->cb(-1, w->data);
}
