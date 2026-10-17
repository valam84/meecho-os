/*
 * usb_hub stand: the hub driver, whole, against a model of the host
 * controller's URB protocol and of the hubs on the wire.
 *
 * usb_hub.c and urb_helper.c are included as they are (main renamed) and
 * built with the host's compiler against the headers in stubs/ and the
 * tree's own DDEKit headers.  The stand plays three parts:
 *
 *   DDEKit      threads that are cooperative, as DDEKit's are: one runs
 *               until it waits on a semaphore or sleeps, then another.
 *               Time is the stand's own and passes only when every thread
 *               waits - so a sleep of a second costs nothing, and an order
 *               of events is the same on every run
 *   xhci        the URB protocol as xhci_urb.c answers it: a request is
 *               accepted at once and completed later, from the message
 *               loop; news is answered at once.  And its one rule for a
 *               client, counted as a violation when broken: nothing is
 *               said to xhci while a request of this process is still
 *               outstanding - xhci completes control transfers with a
 *               synchronous send, which a thread in a sendrec would take
 *               for its reply (libusb panics)
 *   the hubs    answer GET_DESCRIPTOR(hub), GET_STATUS(port) and the port
 *               features, from a model of the ports: power, connection,
 *               reset, enable, over-current, the change bits; ganged power
 *               switches all ports at once.  A device can be plugged in,
 *               pulled out, made to refuse to enable, made to draw too
 *               much
 *
 * Built with -fsanitize=undefined: an index past a port array is a failure
 * here, not a quiet write into the next field.
 *
 *   sh run.sh          the checks, quiet
 *   sh run.sh -v       every check and the driver's own words
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ucontext.h>

#include <ddekit/thread.h>
#include <ddekit/semaphore.h>
#include <ddekit/usb.h>

static void hub_say(const char *fmt, ...);

#define printf hub_say
#define main hub_main
#include "usb_hub.c"
#include "urb_helper.c"
#undef main
#undef printf

static int verbose;
static unsigned checks, failures;

static void
check(int ok, const char *fmt, ...)
{
	va_list ap;

	checks++;
	if (ok && !verbose)
		return;
	if (!ok)
		failures++;
	va_start(ap, fmt);
	printf("%s ", ok ? "ok  " : "FAIL");
	vprintf(fmt, ap);
	printf("\n");
	va_end(ap);
}

static void
section(const char *name)
{
	printf("-- %s\n", name);
}

/*===========================================================================*
 *    what the driver says                                                   *
 *===========================================================================*/
static char said[64 * 1024];
static size_t said_len;

static void
hub_say(const char *fmt, ...)
{
	char line[512];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);
	if (n < 0)
		return;
	if (verbose)
		fputs(line, stderr);
	if (said_len + (size_t)n + 1 < sizeof(said)) {
		memcpy(said + said_len, line, (size_t)n);
		said_len += (size_t)n;
		said[said_len] = '\0';
	}
}

static int
has_said(const char *what)
{
	return strstr(said, what) != NULL;
}

static void
said_reset(void)
{
	said_len = 0;
	said[0] = '\0';
}

static unsigned violations;
static char last_violation[256];

static void
violation(const char *fmt, ...)
{
	va_list ap;

	violations++;
	va_start(ap, fmt);
	vsnprintf(last_violation, sizeof(last_violation), fmt, ap);
	va_end(ap);
	if (verbose)
		fprintf(stderr, "  stand: %s\n", last_violation);
}

void
panic(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fprintf(stderr, "PANIC in the driver: ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
	printf("FAIL the driver panicked\n");
	exit(1);
}

/*===========================================================================*
 *    DDEKit: cooperative threads and stand time                             *
 *===========================================================================*/
#define MAX_THREADS	24
#define STACK		(256 * 1024)

enum { T_FREE, T_READY, T_WAITING, T_SLEEPING, T_DONE };

struct ddekit_thread {
	int state;
	ucontext_t ctx;
	void (*fun)(void *);
	void *arg;
	char name[32];
	unsigned long wake;
	char *stack;
};

struct ddekit_sem {
	int count;
	struct ddekit_thread *wait[MAX_THREADS];
	unsigned nwait;
};

static struct ddekit_thread threads[MAX_THREADS];
static unsigned nthreads;
static struct ddekit_thread *current;
static ucontext_t sched_ctx;
static unsigned long now_ms;

/*
 * The ready threads, first come first served - DDEKit's own order
 * (_ddekit_thread_enqueue() appends).  Which thread runs next is the whole
 * question for the lock on talking to xhci, so it is not left to the order
 * of an array.
 */
static struct ddekit_thread *ready_q[MAX_THREADS];
static unsigned ready_n;

static void
make_ready(struct ddekit_thread *t)
{
	if (t->state == T_READY)
		return;
	t->state = T_READY;
	ready_q[ready_n++] = t;
}

static void
to_scheduler(void)
{
	struct ddekit_thread *t = current;

	if (t == NULL) {
		printf("FAIL a wait outside any thread\n");
		exit(1);
	}
	swapcontext(&t->ctx, &sched_ctx);
}

static void
trampoline(int i)
{
	threads[i].fun(threads[i].arg);
	threads[i].state = T_DONE;
	to_scheduler();
}

ddekit_thread_t *
ddekit_thread_create(void (*fun)(void *), void *arg, const char *name)
{
	struct ddekit_thread *t;

	if (nthreads >= MAX_THREADS) {
		printf("FAIL more threads than the stand holds\n");
		exit(1);
	}
	t = &threads[nthreads];
	memset(t, 0, sizeof(*t));
	t->fun = fun;
	t->arg = arg;
	snprintf(t->name, sizeof(t->name), "%s", name);
	t->stack = malloc(STACK);
	getcontext(&t->ctx);
	t->ctx.uc_stack.ss_sp = t->stack;
	t->ctx.uc_stack.ss_size = STACK;
	t->ctx.uc_link = &sched_ctx;
	makecontext(&t->ctx, (void (*)(void))trampoline, 1, (int)nthreads);
	t->state = T_FREE;
	make_ready(t);
	nthreads++;
	return t;
}

void
ddekit_thread_terminate(ddekit_thread_t *t)
{
	t->state = T_DONE;
}

void
ddekit_thread_msleep(unsigned long msecs)
{
	if (msecs == 0)
		msecs = 1;
	current->wake = now_ms + msecs;
	current->state = T_SLEEPING;
	to_scheduler();
}

ddekit_sem_t *
ddekit_sem_init(int value)
{
	struct ddekit_sem *s = calloc(1, sizeof(*s));

	s->count = value;
	return s;
}

void
ddekit_sem_deinit(ddekit_sem_t *s)
{
	(void)s;
}

void
ddekit_sem_down(ddekit_sem_t *s)
{
	if (s == NULL) {
		printf("FAIL a wait on no semaphore\n");
		exit(1);
	}
	if (s->count > 0) {
		s->count--;
		return;
	}
	s->wait[s->nwait++] = current;
	current->state = T_WAITING;
	to_scheduler();
}

void
ddekit_sem_up(ddekit_sem_t *s)
{
	unsigned i;

	if (s == NULL) {
		violation("a completion for nobody: the semaphore in the URB "
		    "is NULL");
		return;
	}
	if (s->nwait == 0) {
		s->count++;
		return;
	}
	make_ready(s->wait[0]);
	for (i = 1; i < s->nwait; i++)
		s->wait[i - 1] = s->wait[i];
	s->nwait--;

	/*
	 * And the one that raised it gives way, as DDEKit's does
	 * (ddekit_sem_up() calls ddekit_thread_schedule()): the thread just
	 * woken runs before the one that woke it goes on.
	 */
	if (current != NULL) {
		current->state = T_FREE;
		make_ready(current);
		to_scheduler();
	}
}

void ddekit_init(void) { }
void ddekit_shutdown(void) { }

static ddekit_sem_t *never;

void
ddekit_minix_wait_exit(void)
{
	ddekit_sem_down(never);
}

static struct ddekit_usb_driver *driver;

int
ddekit_usb_init(struct ddekit_usb_driver *drv, ddekit_usb_malloc_fn *m,
	ddekit_usb_free_fn *f)
{
	driver = drv;
	ddekit_sem_down(never);
	return 0;
}

void env_setargs(int argc, char **argv) { }
void sef_setcb_init_fresh(int (*cb)(int, sef_init_info_t *)) { }
void sef_setcb_init_lu(int (*cb)(int, sef_init_info_t *)) { }
void sef_setcb_init_restart(int (*cb)(int, sef_init_info_t *)) { }
void sef_setcb_signal_handler(void (*cb)(int)) { }

/*===========================================================================*
 *    the hubs                                                               *
 *===========================================================================*/
#define LOW	0
#define FULL	1
#define HIGH	2

struct mport {
	int attached;		/* a device is plugged in */
	unsigned speed;
	int never_enables;	/* a device whose reset never takes */
	int power, connection, enable, oc;
	int c_conn, c_enable, c_oc, c_reset;
	unsigned reset_left;	/* ms */
	unsigned resets;
	unsigned long power_on_at;
};

struct mhub {
	int nports;
	uint16_t chars;
	uint8_t pgood;
	struct mport port[16];
	unsigned requests, status_reads;
	long first_read_after_power;	/* -1: not yet */
	unsigned long powered_at;
	int powered;
};

/* The stand's own device: what the driver holds a pointer to. */
struct ddekit_usb_dev {
	int id;
	int withdrawn;
	unsigned submits;		/* requests the driver made of it */
	unsigned refuse[16];		/* news of a device here xhci fails */
	struct mhub hub;
};

#define MAX_DEVS	12

static struct ddekit_usb_dev devs[MAX_DEVS];
static unsigned ndevs;

static struct ddekit_usb_dev *
new_hub(int id, int nports, uint16_t chars, uint8_t pgood)
{
	struct ddekit_usb_dev *d;

	if (ndevs >= MAX_DEVS) {
		printf("FAIL more hubs than the stand holds\n");
		exit(1);
	}
	d = &devs[ndevs++];
	memset(d, 0, sizeof(*d));
	d->id = id;
	d->hub.nports = nports;
	d->hub.chars = chars;
	d->hub.pgood = pgood;
	d->hub.first_read_after_power = -1;
	return d;
}

static void
port_power(struct mhub *h, int p, int on)
{
	struct mport *mp = &h->port[p];

	if (on && !mp->power) {
		mp->power = 1;
		mp->power_on_at = now_ms;
		if (mp->attached) {
			mp->connection = 1;
			mp->c_conn = 1;
		}
	} else if (!on && mp->power) {
		mp->power = 0;
		if (mp->connection)
			mp->c_conn = 1;
		mp->connection = mp->enable = 0;
	}
}

static void
plug(struct ddekit_usb_dev *d, int p, unsigned speed)
{
	struct mport *mp = &d->hub.port[p];

	mp->attached = 1;
	mp->speed = speed;
	mp->never_enables = 0;
	if (mp->power) {
		mp->connection = 1;
		mp->c_conn = 1;
	}
}

static void
unplug(struct ddekit_usb_dev *d, int p)
{
	struct mport *mp = &d->hub.port[p];

	mp->attached = 0;
	if (mp->connection)
		mp->c_conn = 1;
	mp->connection = mp->enable = 0;
}

static uint32_t
port_word(const struct mport *mp)
{
	uint32_t w = 0;

	w |= mp->connection ? 1u << 0 : 0;
	w |= mp->enable ? 1u << 1 : 0;
	w |= mp->oc ? 1u << 3 : 0;
	w |= mp->reset_left ? 1u << 4 : 0;
	w |= mp->power ? 1u << 8 : 0;
	if (mp->connection) {
		w |= mp->speed == LOW ? 1u << 9 : 0;
		w |= mp->speed == HIGH ? 1u << 10 : 0;
	}
	w |= mp->c_conn ? 1u << 16 : 0;
	w |= mp->c_enable ? 1u << 17 : 0;
	w |= mp->c_oc ? 1u << 19 : 0;
	w |= mp->c_reset ? 1u << 20 : 0;
	return w;
}

/* Time passing on the wire: resets finish. */
static void
hubs_tick(unsigned ms)
{
	unsigned i;
	int p;

	for (i = 0; i < MAX_DEVS; i++)
		for (p = 1; p <= 15; p++) {
			struct mport *mp = &devs[i].hub.port[p];

			if (mp->reset_left == 0)
				continue;
			if (mp->reset_left > ms) {
				mp->reset_left -= ms;
				continue;
			}
			mp->reset_left = 0;
			mp->c_reset = 1;
			mp->resets++;
			if (mp->connection && !mp->never_enables)
				mp->enable = 1;
		}
}

/* A control request to a hub, as the hub answers it. */
static int
hub_control(struct ddekit_usb_dev *d, const struct usb_ctrlrequest *rq,
	char *data, unsigned size, unsigned *actual)
{
	struct mhub *h = &d->hub;
	int p = rq->wIndex, i;
	struct mport *mp = p >= 1 && p <= 15 ? &h->port[p] : NULL;

	*actual = 0;
	h->requests++;

	if (rq->bRequestType == 0xa0 && rq->bRequest == 6 &&
	    rq->wValue == 0x2900) {
		uint8_t desc[9] = { 9, 0x29, (uint8_t)h->nports,
		    (uint8_t)(h->chars & 0xff), (uint8_t)(h->chars >> 8),
		    h->pgood, 100, 0, 0xff };

		*actual = size < 9 ? size : 9;
		memcpy(data, desc, *actual);
		return 0;
	}

	if (mp == NULL || p > h->nports)
		return EPIPE;		/* a stall: no such port */

	if (rq->bRequestType == 0xa3 && rq->bRequest == 0) {
		uint32_t w = port_word(mp);

		if (size < 4)
			return EPIPE;
		h->status_reads++;
		if (h->first_read_after_power < 0 && h->powered)
			h->first_read_after_power =
			    (long)(now_ms - h->powered_at);
		memcpy(data, &w, 4);
		*actual = 4;
		return 0;
	}

	if (rq->bRequestType != 0x23 || (rq->bRequest != 1 &&
	    rq->bRequest != 3))
		return EPIPE;

	if (rq->bRequest == 3) {		/* SET_FEATURE */
		switch (rq->wValue) {
		case 8:				/* PORT_POWER */
			if ((h->chars & 3) == 0) {
				for (i = 1; i <= h->nports; i++)
					if (!h->port[i].oc)
						port_power(h, i, 1);
			} else if (!mp->oc)
				port_power(h, p, 1);
			h->powered = 1;
			h->powered_at = now_ms;
			return 0;
		case 4:				/* PORT_RESET */
			if (mp->connection)
				mp->reset_left = 20;
			return 0;
		default:
			return EPIPE;
		}
	}

	switch (rq->wValue) {			/* CLEAR_FEATURE */
	case 1:		mp->enable = 0; return 0;
	case 8:		port_power(h, p, 0); return 0;
	case 16:	mp->c_conn = 0; return 0;
	case 17:	mp->c_enable = 0; return 0;
	case 18:	return 0;
	case 19:	mp->c_oc = 0; return 0;
	case 20:	mp->c_reset = 0; return 0;
	default:	return EPIPE;
	}
}

/*===========================================================================*
 *    xhci                                                                   *
 *===========================================================================*/
#define MAX_PENDING	8

/*
 * A request takes a millisecond of stand time on the wire.  Not
 * decoration: with a completion that arrives before any other thread has
 * had a turn, two hubs' requests can never be out at once, and a driver
 * that does not keep to xhci's rule cannot be told from one that does.
 */
#define WIRE_MS		1

static struct ddekit_usb_urb *pending[MAX_PENDING];
static unsigned long pending_due[MAX_PENDING];
static unsigned npending;
static unsigned outstanding;		/* accepted and not yet completed */

int
ddekit_usb_submit_urb(struct ddekit_usb_urb *urb)
{
	if (outstanding != 0)
		violation("a request while %u of this process's are still "
		    "outstanding", outstanding);
	if (npending >= MAX_PENDING) {
		printf("FAIL more requests than the stand holds\n");
		exit(1);
	}
	urb->dev->submits++;
	pending_due[npending] = now_ms + WIRE_MS;
	pending[npending++] = urb;
	outstanding++;
	return 0;
}

/* The message loop: one completion delivered, as xhci would send it. */
static int
deliver(void)
{
	struct ddekit_usb_urb *urb;
	struct ddekit_usb_dev *d;
	unsigned i, actual = 0;
	int r;

	if (npending == 0 || pending_due[0] > now_ms)
		return 0;
	urb = pending[0];
	for (i = 1; i < npending; i++) {
		pending[i - 1] = pending[i];
		pending_due[i - 1] = pending_due[i];
	}
	npending--;

	d = urb->dev;
	if (d->withdrawn)
		r = ENODEV;
	else
		r = hub_control(d, (struct usb_ctrlrequest *)urb->setup_packet,
		    urb->data, urb->size, &actual);

	urb->status = r;
	urb->actual_length = r == 0 ? actual : 0;
	outstanding--;
	driver->completion(urb->priv);
	return 1;
}

struct news {
	int id;
	long type;
	long port;
	unsigned long at;
};

static struct news news[256];
static unsigned nnews;

long
ddekit_usb_info(struct ddekit_usb_dev *dev, long type, long value)
{
	if (outstanding != 0)
		violation("news while %u request(s) of this process are still "
		    "outstanding", outstanding);
	if (dev == NULL)
		violation("news that names no hub");
	if (dev != NULL && dev->withdrawn)
		violation("news about hub %d after it was withdrawn",
		    dev->id);
	if (nnews < 256) {
		news[nnews].id = dev != NULL ? dev->id : 0;
		news[nnews].type = type;
		news[nnews].port = value;
		news[nnews].at = now_ms;
		nnews++;
	}

	/*
	 * A device xhci could not address - the dock's RTL8152 on the board,
	 * Address Device ending in a transaction error - is answered with
	 * EIO, as xhci_urb.c answers it.
	 */
	if (dev != NULL && type != DDEKIT_HUB_PORT_DISCONN && value >= 0 &&
	    value < 16 && dev->refuse[value] != 0) {
		dev->refuse[value]--;
		return EIO;
	}
	return 0;
}

static int
has_news(int id, long type, long port)
{
	unsigned i;
	int n = 0;

	for (i = 0; i < nnews; i++)
		if (news[i].id == id && news[i].type == type &&
		    news[i].port == port)
			n++;
	return n;
}

static int
news_from(int id, unsigned long since)
{
	unsigned i;
	int n = 0;

	for (i = 0; i < nnews; i++)
		if (news[i].id == id && news[i].at >= since)
			n++;
	return n;
}

/*===========================================================================*
 *    the scheduler                                                          *
 *===========================================================================*/
static int
run_ready(void)
{
	struct ddekit_thread *t;
	unsigned i;

	if (ready_n == 0)
		return 0;
	t = ready_q[0];
	for (i = 1; i < ready_n; i++)
		ready_q[i - 1] = ready_q[i];
	ready_n--;

	t->state = T_FREE;		/* running; it says what it waits for */
	current = t;
	swapcontext(&sched_ctx, &t->ctx);
	current = NULL;
	return 1;
}

/* Let ms of stand time pass, running everything that can run. */
static void
run_for(unsigned long ms)
{
	unsigned long end = now_ms + ms;

	for (;;) {
		unsigned long next = end;
		unsigned i;

		if (deliver() || run_ready())
			continue;
		if (now_ms >= end)
			break;

		/* Everybody waits: on to the next thing that happens. */
		for (i = 0; i < nthreads; i++)
			if (threads[i].state == T_SLEEPING &&
			    threads[i].wake < next)
				next = threads[i].wake;
		if (npending != 0 && pending_due[0] < next)
			next = pending_due[0];
		if (next > now_ms) {
			hubs_tick((unsigned)(next - now_ms));
			now_ms = next;
		}
		for (i = 0; i < nthreads; i++)
			if (threads[i].state == T_SLEEPING &&
			    threads[i].wake <= now_ms)
				make_ready(&threads[i]);
	}
}

static int
entries_used(void)
{
	unsigned i;
	int n = 0;

	for (i = 0; i < USB_HUB_MAX; i++)
		if (hubs[i].used)
			n++;
	return n;
}

static int
all_powered(struct ddekit_usb_dev *d)
{
	int p;

	for (p = 1; p <= d->hub.nports; p++)
		if (!d->hub.port[p].power)
			return 0;
	return 1;
}

static void
hub_main_thread(void *arg)
{
	hub_main(1, (char **)arg);
}

static void
boot(void)
{
	static char *argv[] = { "usb_hub", NULL };

	never = ddekit_sem_init(0);
	ddekit_thread_create(hub_main_thread, argv, "main");
	run_for(10);
}

/*===========================================================================*
 *    the checks                                                             *
 *===========================================================================*/
/*
 * The dock as the board's Linux saw it (port/cb2-usb/dock-reference.txt):
 * the board's hub with the keyboard and mouse receiver on port 3 and the
 * dock's hub A on port 1; A with hub B on its port 3 and the card reader
 * on its port 4; B with the RTL8152 on its port 3 - and here a low-speed
 * keyboard on B's port 1 as well.  The hubs arrive as xhci would give
 * them: each once its parent has reported it.
 */
static void
test_dock(struct ddekit_usb_dev *a, struct ddekit_usb_dev *b1,
	struct ddekit_usb_dev *b2)
{
	unsigned th;

	section("the dock: the board's hub, and two of the dock's below it");

	plug(a, 1, HIGH);		/* the dock's hub A */
	plug(a, 3, FULL);		/* the receiver */
	plug(b1, 3, HIGH);		/* hub B */
	plug(b1, 4, HIGH);		/* the card reader */
	plug(b2, 3, HIGH);		/* the RTL8152 */
	plug(b2, 1, LOW);

	th = nthreads;
	driver->connect(a, 1);
	run_for(2000);
	check(has_news(a->id, DDEKIT_HUB_PORT_HS_CONN, 1) == 1 &&
	    has_news(a->id, DDEKIT_HUB_PORT_FS_CONN, 3) == 1,
	    "the board's hub reports the dock's hub and the receiver");

	driver->connect(b1, 1);
	run_for(2000);
	check(has_news(b1->id, DDEKIT_HUB_PORT_HS_CONN, 3) == 1 &&
	    has_news(b1->id, DDEKIT_HUB_PORT_HS_CONN, 4) == 1,
	    "hub A reports hub B and the card reader, naming hub A");

	driver->connect(b2, 1);
	run_for(2000);
	check(has_news(b2->id, DDEKIT_HUB_PORT_HS_CONN, 3) == 1,
	    "hub B reports the RTL8152, naming hub B");
	check(has_news(b2->id, DDEKIT_HUB_PORT_LS_CONN, 1) == 1,
	    "and the low-speed keyboard, too");

	check(nthreads == th + 3, "a thread for each hub (%u)",
	    nthreads - th);
	check(nnews == 6, "and nothing else was said (%u)", nnews);
	check(all_powered(a) && all_powered(b1) && all_powered(b2),
	    "every port of the three hubs is powered - a phone on the dock "
	    "charges whatever is on the other ports");
	check(a->hub.first_read_after_power >= 100 &&
	    b1->hub.first_read_after_power >= 100 &&
	    b2->hub.first_read_after_power >= 100,
	    "and no port was looked at before its power was good (%ld, %ld, "
	    "%ld ms; the dock's hubs say 96, the floor is 100)",
	    a->hub.first_read_after_power, b1->hub.first_read_after_power,
	    b2->hub.first_read_after_power);
	check(b2->hub.port[3].resets == 1 && b1->hub.port[3].resets == 1 &&
	    a->hub.port[1].resets == 1, "each device was reset once");
	check(violations == 0, "the driver kept to xhci's rule: %s",
	    violations ? last_violation : "one conversation at a time");
	check(has_said("hub 2: 4 port(s), ganged power switching, global "
	    "over-current protection, think time 1, power good in 96 ms"),
	    "and said how the dock's hubs switch their power");
}

/* xhci withdraws the farthest first: hub B, then hub A. */
static void
test_withdrawn(struct ddekit_usb_dev *a, struct ddekit_usb_dev *b1,
	struct ddekit_usb_dev *b2)
{
	unsigned long t;
	unsigned a_before, b1_before, b2_before;

	section("the dock pulled out");

	b1->withdrawn = b2->withdrawn = 1;
	t = now_ms;
	a_before = a->hub.requests;
	b1_before = b1->submits;
	b2_before = b2->submits;
	driver->disconnect(b2);
	driver->disconnect(b1);
	run_for(3000);

	check(news_from(b1->id, t) == 0 && news_from(b2->id, t) == 0,
	    "nothing more is said about either of the dock's hubs");
	check(b1->submits == b1_before && b2->submits == b2_before,
	    "nothing more is asked of them (%u, %u)",
	    b1->submits - b1_before, b2->submits - b2_before);
	check(entries_used() == 1, "their entries are free again (%d in use)",
	    entries_used());
	check(a->hub.requests > a_before, "and the board's hub is still "
	    "polled (%u requests)", a->hub.requests - a_before);
	check(violations == 0, "the driver kept to xhci's rule: %s",
	    violations ? last_violation : "yes");
}

/*
 * The dock's hubs given at once, each with its device - which is what xhci
 * does for a driver that starts again: every hub it has, one announcement
 * after another.  Their threads then work side by side from the first
 * poll, and this is where the lock on news earns its keep: one thread
 * reporting while the other's request is out.
 */
static void
test_at_once(struct ddekit_usb_dev *b1, struct ddekit_usb_dev *b2)
{
	section("both of the dock's hubs given at once");

	b1->withdrawn = b2->withdrawn = 0;
	memset(&b1->hub.port, 0, sizeof(b1->hub.port));
	memset(&b2->hub.port, 0, sizeof(b2->hub.port));
	b1->hub.powered = b2->hub.powered = 0;
	plug(b1, 3, HIGH);
	plug(b2, 3, HIGH);
	nnews = 0;

	driver->connect(b1, 1);
	driver->connect(b2, 1);
	run_for(3000);

	check(has_news(b1->id, DDEKIT_HUB_PORT_HS_CONN, 3) == 1 &&
	    has_news(b2->id, DDEKIT_HUB_PORT_HS_CONN, 3) == 1 && nnews == 2,
	    "each hub's news names that hub (%u in all)", nnews);
	check(violations == 0, "the driver kept to xhci's rule: %s",
	    violations ? last_violation : "yes");

	b1->withdrawn = b2->withdrawn = 1;
	driver->disconnect(b2);
	driver->disconnect(b1);
	run_for(2000);
	check(entries_used() == 1, "and withdrawn again, their entries are "
	    "free (%d in use)", entries_used());
}

/*
 * A device the host controller cannot address at first: the dock's RTL8152
 * on the board.  The port is reset and the device reported again, up to
 * four times, as Linux does; one that never takes an address has its port
 * disabled and left powered.
 */
static void
test_not_addressed(struct ddekit_usb_dev *c)
{
	struct mport *mp = &c->hub.port[1];
	unsigned n = nnews, resets;

	section("a device the host controller could not address");

	said_reset();
	resets = mp->resets;
	c->refuse[1] = 1;
	plug(c, 1, HIGH);
	run_for(4000);

	check(has_news(c->id, DDEKIT_HUB_PORT_HS_CONN, 1) == 2,
	    "refused once, it is reported twice (%u news)", nnews - n);
	check(mp->resets == resets + 2, "with a reset before each (%u)",
	    mp->resets - resets);
	check(has_said("resetting again"), "and the driver says why");

	said_reset();
	unplug(c, 1);
	run_for(2000);
	n = nnews;
	resets = mp->resets;
	c->refuse[1] = 10;
	plug(c, 1, HIGH);
	run_for(8000);
	check(has_news(c->id, DDEKIT_HUB_PORT_HS_CONN, 1) >= 4 &&
	    mp->resets == resets + 4, "never addressed: four tries, four "
	    "resets (%u, %u)", nnews - n, mp->resets - resets);
	check(has_said("given up") && mp->power && !mp->enable,
	    "then given up on - the port disabled, and powered");

	c->refuse[1] = 0;
	unplug(c, 1);
	run_for(2000);
	check(violations == 0, "the driver kept to xhci's rule: %s",
	    violations ? last_violation : "yes");
}

static void
test_new_dock(struct ddekit_usb_dev *c)
{
	unsigned th = nthreads;

	section("another hub plugged in: a new name, a free entry");

	plug(c, 4, FULL);
	driver->connect(c, 1);
	run_for(3000);

	check(nthreads == th, "no new thread: the free entry's thread takes "
	    "it (%u new)", nthreads - th);
	check(has_news(c->id, DDEKIT_HUB_PORT_FS_CONN, 4) == 1,
	    "news of its device names the new hub");
	check(entries_used() == 2, "two entries in use (%d)", entries_used());
	check(violations == 0, "the driver kept to xhci's rule: %s",
	    violations ? last_violation : "yes");
}

static void
test_overcurrent(struct ddekit_usb_dev *c)
{
	struct mport *mp = &c->hub.port[3];

	section("over-current on a port of the dock");

	said_reset();
	mp->oc = 1;
	mp->c_oc = 1;
	port_power(&c->hub, 3, 0);
	run_for(2000);

	check(mp->c_oc == 0, "the change is acknowledged");
	check(!mp->power, "and the port stays off while the over-current "
	    "lasts");
	check(has_said("over-current now"), "which the driver says");

	mp->oc = 0;
	mp->c_oc = 1;
	run_for(2000);

	check(mp->power, "when it clears, the port is powered again");
	check(has_said("powered again"), "and the driver says so");
	check(violations == 0, "the driver kept to xhci's rule: %s",
	    violations ? last_violation : "yes");
}

static void
test_refusing_device(struct ddekit_usb_dev *c)
{
	struct mport *mp = &c->hub.port[2];
	unsigned n;

	section("a device that will not enable keeps its port's power");

	said_reset();
	n = nnews;
	plug(c, 2, HIGH);
	mp->never_enables = 1;
	run_for(4000);

	check(has_news(c->id, DDEKIT_HUB_PORT_HS_CONN, 2) == 0,
	    "nothing is reported for it");
	check(mp->power, "its port stays powered - a phone there charges");
	check(!mp->enable, "and disabled");
	check(has_said("stays powered"), "which the driver says");

	unplug(c, 2);
	run_for(2000);
	check(has_said("the port is in use again"), "unplugged, the port is "
	    "given up on no longer");

	plug(c, 2, HIGH);
	run_for(3000);
	check(has_news(c->id, DDEKIT_HUB_PORT_HS_CONN, 2) == 1,
	    "and a device that works there is reported (%u news)",
	    nnews - n);
	check(violations == 0, "the driver kept to xhci's rule: %s",
	    violations ? last_violation : "yes");
}

/*
 * What a restarted driver finds: devices whose connection its predecessor
 * acknowledged, so that no change bit says they are there.
 */
static void
test_restart(struct ddekit_usb_dev *d)
{
	struct mport *mp = &d->hub.port[1];

	section("a hub given to a driver that knows nothing of its ports");

	check(hub_sef_hdlr(SEF_INIT_RESTART, NULL) == EXIT_SUCCESS,
	    "a restart is taken as a fresh start");

	d->hub.chars = 0x0001;
	mp->power = 1;
	mp->attached = 1;
	mp->speed = HIGH;
	mp->connection = 1;
	mp->enable = 1;
	mp->c_conn = 0;
	driver->connect(d, 1);
	run_for(3000);

	check(has_news(d->id, DDEKIT_HUB_PORT_HS_CONN, 1) == 1,
	    "a device on a port with no change reported is reported found");
	check(mp->resets == 1, "after a reset of its own");

	/* And gone, again without the hub saying so. */
	mp->attached = 0;
	mp->connection = 0;
	mp->enable = 0;
	mp->c_conn = 0;
	run_for(2000);
	check(has_news(d->id, DDEKIT_HUB_PORT_DISCONN, 1) == 1,
	    "gone with no change reported, it is reported gone");
	check(violations == 0, "the driver kept to xhci's rule: %s",
	    violations ? last_violation : "yes");
}

static void
test_port_counts(void)
{
	struct ddekit_usb_dev *big, *huge;
	int p;

	section("fifteen ports, and sixteen");

	big = new_hub(31, 15, 0x0001, 50);
	plug(big, 15, HIGH);
	driver->connect(big, 1);
	run_for(3000);
	check(has_news(31, DDEKIT_HUB_PORT_HS_CONN, 15) == 1,
	    "a device on port 15 of a fifteen-port hub is reported");
	for (p = 1; p <= 15 && big->hub.port[p].power; p++)
		;
	check(p == 16, "and all fifteen ports are powered");

	said_reset();
	huge = new_hub(32, 16, 0x0001, 50);
	driver->connect(huge, 1);
	run_for(2000);
	check(has_said("Too many hub ports declared: 16"),
	    "sixteen is more than a route string can reach, and said so");
	check(huge->hub.requests == 1, "nothing is asked of it after its "
	    "descriptor (%u)", huge->hub.requests);
}

static void
test_too_many(void)
{
	int i, used;

	section("more hubs than entries");

	said_reset();
	used = entries_used();
	for (i = 0; i < USB_HUB_MAX - used; i++)
		driver->connect(new_hub(40 + i, 2, 0x0001, 50), 1);
	run_for(500);
	check(entries_used() == USB_HUB_MAX, "every entry is taken (%d)",
	    entries_used());
	driver->connect(new_hub(60, 2, 0x0001, 50), 1);
	run_for(500);
	check(has_said("it is not driven"), "one more is told about, not "
	    "driven - and the driver lives");
}

/*===========================================================================*
 *    main                                                                   *
 *===========================================================================*/
int
main(int argc, char **argv)
{
	struct ddekit_usb_dev *a, *b1, *b2, *c, *d;

	if (argc > 1 && strcmp(argv[1], "-v") == 0)
		verbose = 1;

	section("the driver comes up");
	boot();
	check(driver != NULL, "and has said hello to the host controller");
	if (driver == NULL)
		return 1;

	/*
	 * The board's hub as port/cb2-usb/reference.txt has it: four
	 * ports, ganged power, power good in 100 ms.  The dock's two hubs
	 * as dock-reference.txt has them: WCH, four ports, ganged power and
	 * think time 1 (0x0020), power good in 96 ms.
	 */
	a = new_hub(11, 4, 0x0000, 50);
	b1 = new_hub(12, 4, 0x0020, 48);
	b2 = new_hub(15, 4, 0x0020, 48);
	test_dock(a, b1, b2);
	test_withdrawn(a, b1, b2);
	test_at_once(b1, b2);

	/*
	 * A hub of the other kind, power switched per port - none of the
	 * board's or the dock's is one - for what a port alone can do.
	 */
	c = new_hub(13, 4, 0x0001, 50);
	test_new_dock(c);
	test_overcurrent(c);
	test_refusing_device(c);
	test_not_addressed(c);

	d = new_hub(14, 4, 0x0001, 50);
	test_restart(d);

	test_port_counts();
	test_too_many();

	check(violations == 0, "and through all of it, xhci's rule was kept: "
	    "%s", violations ? last_violation : "yes");

	printf("\n%u checks, %u failure(s)\n", checks, failures);
	return failures != 0 ? 1 : 0;
}
