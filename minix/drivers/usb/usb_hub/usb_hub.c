/*
 * Minix3 USB hub driver implementation
 *
 * Every hub the host controller has, in one instance of this driver.
 *
 * It drove one hub, and a second was worse than ignored: "HUB device
 * driver can be connected only once!", and RS restarting a driver that the
 * host controller would offer the second hub to again, for ever.  The
 * CB2's root port carries a Terminus hub, and a USB-C dock plugged into it
 * is a hub of its own with an Ethernet chip and a card reader behind it;
 * so the question was how to drive more than one, and the answer here is
 * one instance holding all of them, a thread each, rather than one
 * instance per hub with a label each.  Three reasons, written down because
 * the other way looks tidier:
 *
 *   Nothing here can start a driver when a device arrives.  The system
 *   this stack came from had devman for that; this one has RS starting
 *   what rc says.  An instance per hub means rc starting some number of
 *   them in advance and xhci dealing hubs out among them - and a dock
 *   plugged into a board whose instances are all taken finds none.
 *
 *   Hubs are a tree, and a hub's devices go when it goes: a crash of the
 *   driver of the board's hub resets everything behind it however many
 *   processes the hubs below were in.  Separate processes would isolate
 *   nothing that is not lost anyway.
 *
 *   What one instance costs is one rule, and it is kept in one place.
 *   xhci finishes a control transfer with a synchronous send, and a
 *   client in a sendrec takes ANY message from xhci for its reply -
 *   libusb then panics.  A single-threaded client never has a request
 *   out while it is in a sendrec; two threads of one process can.  So
 *   everything this driver says to xhci - requests and news - goes
 *   through one lock, "bus": one conversation with xhci at a time from
 *   the whole process, which is all xhci serves anyway (one transfer at
 *   a time, see xhci_urb.c).
 *
 * Linux does the same for the same reasons: one hub_wq for every hub.
 *
 * Each hub's news names it (ddekit_usb_info() passes its device), and xhci
 * believes news about a hub only from the driver it gave the hub to.  A hub
 * that goes is withdrawn (USB_WITHDRAW_DEV); its thread stops at its next
 * step and its entry is free for the next hub.  The thread itself stays,
 * waiting for one: a DDEKit thread cannot end without leaving its stack
 * behind.
 */

#include <string.h>			/* memset */
#include <stdint.h>

#include <ddekit/thread.h>
#include <minix/sef.h>
#include <minix/sysutil.h>		/* panic */
#include <minix/usb.h>			/* usb_ctrlrequest TODO: remove me */

#include "common.h"
#include "urb_helper.h"


/*---------------------------*
 *    declared functions     *
 *---------------------------*/
/* TODO: these are missing from DDE header files */
extern void ddekit_minix_wait_exit(void);
extern void ddekit_shutdown(void);

/* SEF related functions */
static int hub_sef_hdlr(int, sef_init_info_t *);
static void hub_signal_handler(int);

/* DDEKit IPC related */
static void ddekit_usb_task(void *);

/* DDEKit's USB driver callbacks */
static void usb_driver_completion(void *);
static void usb_driver_connect(struct ddekit_usb_dev *, unsigned int);
static void usb_driver_disconnect(struct ddekit_usb_dev *);

/* A hub's own thread */
static void hub_thread(void *);


/*---------------------------*
 *    class specific stuff   *
 *---------------------------*/
#define HUB_PACKED __attribute__((__packed__))

/* How often to check for changes */
#define USB_HUB_POLLING_INTERVAL 1000

/*
 * Max number of hub ports.  Fifteen is what the host controller can reach:
 * the route string that says which port to take at each hub has four bits
 * per hub (xHCI 1.1, 8.9).  It was eight, and the array below was sized to
 * eight and indexed from one, so a hub of eight ports wrote one past it.
 */
#define USB_HUB_PORT_LIMIT 15

/* How many hubs one instance holds at once: the board's, a dock's, and
 * room for what is plugged into those. */
#define USB_HUB_MAX 8

/* Limits number of communication retries (when needed) */
#define USB_HUB_MAX_TRIES 3

/* How long to wait between retries, in case of reset error (ms) */
#define USB_HUB_RESET_DELAY 200

/* How long a connection must hold still before it is reset (USB 2.0,
 * 7.1.7.3: TATTDB), in what steps it is watched, and when to give up. */
#define USB_HUB_DEBOUNCE_STEP 25	/* ms */
#define USB_HUB_DEBOUNCE_STABLE 100	/* ms */
#define USB_HUB_DEBOUNCE_LIMIT 1500	/* ms */

/*
 * How long after the ports are powered before anything on them is looked
 * at: the hub's own bPwrOn2PwrGood (in units of 2 ms), and never less than
 * 100 ms - the floor Linux keeps too (hub_power_on_good_delay()), because
 * hubs that say 0 exist.  And how long a port is left off after an
 * over-current before it is powered again (Linux: 100 ms as well).
 */
#define USB_HUB_POWER_GOOD_MIN 100	/* ms */
#define USB_HUB_OVERCURRENT_REST 100	/* ms */

/* A third answer from hub_handle_connection(): the device is no longer
 * there, which is a disconnection and not a bad port. */
#define HUB_CONN_GONE 2

/* How many times a port is reset before it is given up on.  Five is what
 * Linux allows (PORT_RESET_TRIES); on the board three were once not
 * enough for a flash drive that dropped off the bus on every reset. */
#define USB_HUB_RESET_ROUNDS 5

/* How many times a device is reset and reported before the host
 * controller is believed that it cannot be addressed: Linux's
 * PORT_INIT_TRIES. */
#define USB_HUB_ENUM_TRIES 4

/* Hub descriptor type */
#define USB_HUB_DESCRIPTOR_TYPE 0x29

/* Hub descriptor structure */
typedef struct HUB_PACKED hub_descriptor {

	uint8_t		bDescLength;
	uint8_t		bDescriptorType;
	uint8_t		bNbrPorts;
	uint16_t	wHubCharacteristics;
	uint8_t		bPwrOn2PwrGood;
	uint8_t		bHubContrCurrent;
	/* Remaining variable length fields are ignored for now */
}
hub_descriptor;

/*
 * wHubCharacteristics (USB 2.0, 11.23.2.1): how the hub switches the power
 * of its ports, and how it guards against over-current.  Bit 1 set means
 * no switching at all - the ports are powered whenever the hub is.
 */
#define HUB_CHAR_POWER(c)	((c) & 0x3)
#define HUB_CHAR_POWER_GANGED	0
#define HUB_CHAR_POWER_PORT	1
#define HUB_CHAR_OC(c)		(((c) >> 3) & 0x3)
#define HUB_CHAR_TTT(c)		(((c) >> 5) & 0x3)

/* Hub port status structure, as defined in USB 2.0 document */
typedef struct HUB_PACKED hub_port_status {

	uint32_t	PORT_CONNECTION : 1;
	uint32_t	PORT_ENABLE : 1;
	uint32_t	PORT_SUSPEND : 1;
	uint32_t	PORT_OVER_CURRENT : 1;
	uint32_t	PORT_RESET : 1;
	uint32_t	RESERVED1 : 3;

	uint32_t	PORT_POWER : 1;
	uint32_t	PORT_LOW_SPEED : 1;
	uint32_t	PORT_HIGH_SPEED : 1;
	uint32_t	PORT_TEST : 1;
	uint32_t	PORT_INDICATOR : 1;
	uint32_t	RESERVED2 : 3;

	uint32_t	C_PORT_CONNECTION : 1;
	uint32_t	C_PORT_ENABLE : 1;
	uint32_t	C_PORT_SUSPEND : 1;
	uint32_t	C_PORT_OVER_CURRENT : 1;
	uint32_t	C_PORT_RESET : 1;
	uint32_t	RESERVED3 : 11;
}
hub_port_status;

/* Hub Class Feature Selectors */
typedef enum {

	C_HUB_LOCAL_POWER	= 0 ,
	C_HUB_OVER_CURRENT	= 1 ,
	PORT_CONNECTION		= 0 ,
	PORT_ENABLE		= 1 ,
	PORT_SUSPEND		= 2 ,
	PORT_OVER_CURRENT	= 3 ,
	PORT_RESET		= 4 ,
	PORT_POWER		= 8 ,
	PORT_LOW_SPEED		= 9 ,
	C_PORT_CONNECTION	= 16,
	C_PORT_ENABLE		= 17,
	C_PORT_SUSPEND		= 18,
	C_PORT_OVER_CURRENT	= 19,
	C_PORT_RESET		= 20,
	PORT_TEST		= 21,
	PORT_INDICATOR		= 22
}
class_feature;

/* Hub Class Request Codes */
typedef enum {

	GET_STATUS		= 0 ,
	CLEAR_FEATURE		= 1 ,
	RESERVED1		= 2 ,
	SET_FEATURE		= 3 ,
	RESERVED2		= 4 ,
	RESERVED3		= 5 ,
	GET_DESCRIPTOR		= 6 ,
	SET_DESCRIPTOR		= 7 ,
	CLEAR_TT_BUFFER		= 8 ,
	RESET_TT		= 9 ,
	GET_TT_STATE		= 10,
	STOP_TT			= 11
}
class_code;

/* Hub port connection state */
typedef enum {

	HUB_PORT_DISCONN = 0,
	HUB_PORT_CONN = 1,
	HUB_PORT_ERROR = 2
}
port_conn;

/* Hub port connection changes */
typedef enum {

	HUB_CHANGE_NONE = 0,		/* Nothing changed since last poll */
	HUB_CHANGE_CONN = 1,		/* Device was just connected */
	HUB_CHANGE_DISCONN= 2,		/* Device was just disconnected */
	HUB_CHANGE_STATUS_ERR = 3,	/* Port status mismatch */
	HUB_CHANGE_COM_ERR = 4		/* Something wrong happened to driver */
}
port_change;


/*---------------------------*
 *    defined variables      *
 *---------------------------*/
/* One hub's state */
typedef struct hub_state {

	int used;			/* given a hub and not yet done with it */
	int gone;			/* withdrawn: stop at the next step */
	unsigned num;			/* for the log: hub 1, hub 2... */
	struct ddekit_usb_dev * dev;	/* DDEKit device */
	ddekit_sem_t * done;		/* this hub's request has completed */
	ddekit_sem_t * given;		/* a hub was given to this entry */
	ddekit_thread_t * thread;	/* created with the first hub */
	hub_descriptor descriptor;	/* Class specific descriptor */
	int num_ports;			/* Number of hub ports */
	port_conn conn[USB_HUB_PORT_LIMIT + 1];	/* by port number, from 1 */
}
hub_state;

static hub_state hubs[USB_HUB_MAX];

/* One conversation with the host controller at a time; see the top. */
static ddekit_sem_t * bus = NULL;

/* USB callback structure */
static struct ddekit_usb_driver usb_driver = {
	.completion	= usb_driver_completion,
	.connect	= usb_driver_connect,
	.disconnect	= usb_driver_disconnect
};

/* DDEKit USB message handling thread */
ddekit_thread_t * ddekit_usb_thread = NULL;

#define HUB_PMSG(s, fmt, ...) \
	HUB_MSG("hub %u: " fmt, (s)->num, ##__VA_ARGS__)

/* Hub get class specific descriptor call */
static int hub_get_descriptor(hub_state *, hub_descriptor *);

/* Hub Set/ClearPortFeature call */
static int hub_port_feature(hub_state *, int, class_code, class_feature);

/* Hub GetPortStatus call */
static int hub_get_port_status(hub_state *, int, hub_port_status *);

/* Handle port status change */
static port_change hub_handle_change(hub_state *, int, hub_port_status *);

/* Handle port connection */
static int hub_handle_connection(hub_state *, int, hub_port_status *);

/* Handle port disconnection */
static int hub_handle_disconnection(hub_state *, int);


/*---------------------------*
 *    defined functions      *
 *---------------------------*/
/*===========================================================================*
 *    main                                                                   *
 *===========================================================================*/
int
main(int argc, char * argv[])
{
	unsigned i;

	HUB_MSG("Starting driver... (built: %s %s)", __DATE__, __TIME__);

	/* Store arguments for future parsing */
	env_setargs(argc, argv);

	/* Clear current state */
	memset(hubs, 0, sizeof(hubs));
	for (i = 0; i < USB_HUB_MAX; i++)
		hubs[i].num = i + 1;

	/* Initialize SEF related callbacks */
	sef_setcb_init_fresh(hub_sef_hdlr);
	sef_setcb_init_lu(hub_sef_hdlr);
	sef_setcb_init_restart(hub_sef_hdlr);
	sef_setcb_signal_handler(hub_signal_handler);

	/* Initialize DDEkit (involves sef_startup()) */
	ddekit_init();
	HUB_DEBUG_MSG("DDEkit ready...");

	/* The lock on talking to the host controller: free to begin with */
	bus = ddekit_sem_init(1);
	if (NULL == bus)
		panic("Initializing USB hub semaphore, failed!");

	/* Run USB IPC task to collect messages */
	ddekit_usb_thread = ddekit_thread_create(ddekit_usb_task, NULL,
						"ddekit_task" );
	if (NULL == ddekit_usb_thread)
		panic("Initializing ddekit_usb_thread failed!");

	HUB_DEBUG_MSG("USB IPC task ready...");

	/* Block and wait until exit signal is received */
	ddekit_minix_wait_exit();
	HUB_DEBUG_MSG("Exiting...");

	/* Release objects that were explicitly allocated above */
	ddekit_thread_terminate(ddekit_usb_thread);
	for (i = 0; i < USB_HUB_MAX; i++)
		if (NULL != hubs[i].thread)
			ddekit_thread_terminate(hubs[i].thread);
	ddekit_sem_deinit(bus);

	/* TODO: No ddekit_deinit for proper cleanup? */

	HUB_DEBUG_MSG("Cleanup completed...");

	return EXIT_SUCCESS;
}


/*===========================================================================*
 *    hub_sef_hdlr                                                           *
 *===========================================================================*/
/*
 * A restart is a fresh start.  The driver keeps nothing the hubs do not
 * tell it again: xhci gives a restarted driver every hub it has (it says
 * hello under the same label), the driver finds devices on ports it knows
 * nothing about and reports them as found, and xhci takes that as the
 * device having been reset - which it then is, by hub_handle_connection().
 * Refusing the restart, as this did, left RS restarting a driver that
 * could not start.
 */
static int
hub_sef_hdlr(int type, sef_init_info_t * UNUSED(info))
{
	HUB_DEBUG_DUMP;

	switch (type) {
		case SEF_INIT_FRESH:
		case SEF_INIT_RESTART:
			return EXIT_SUCCESS;
		case SEF_INIT_LU:
			HUB_MSG("Live update is not supported");
			break;
		default:
			HUB_MSG("Illegal SEF type");
			break;
	}

	return EXIT_FAILURE;
}


/*===========================================================================*
 *    hub_signal_handler                                                     *
 *===========================================================================*/
static void
hub_signal_handler(int this_signal)
{
	HUB_DEBUG_DUMP;

	HUB_MSG("Handling signal 0x%X", this_signal);

	/* TODO: Any signal means shutdown for now (it may be OK anyway) */
	/* Try graceful DDEKit exit */
	ddekit_shutdown();

	/* Unreachable, when ddekit_shutdown works correctly */
	panic("Calling ddekit_shutdown failed!");
}


/*===========================================================================*
 *    ddekit_usb_task                                                        *
 *===========================================================================*/
static void
ddekit_usb_task(void * UNUSED(arg))
{
	HUB_DEBUG_DUMP;

	/* TODO: This call was meant to return 'int' but loops forever instead,
	 * so no return value is checked */
	ddekit_usb_init(&usb_driver, NULL, NULL);
}


/*===========================================================================*
 *    usb_driver_completion                                                  *
 *===========================================================================*/
static void
usb_driver_completion(void * priv)
{
	HUB_DEBUG_DUMP;

	/* The request of whichever hub's thread asked (blocking_urb_submit()
	 * put its semaphore in the URB) has completed */
	ddekit_sem_up((ddekit_sem_t *)priv);
}


/*===========================================================================*
 *    usb_driver_connect                                                     *
 *===========================================================================*/
static void
usb_driver_connect(struct ddekit_usb_dev * dev, unsigned int UNUSED(interfaces))
{
	hub_state * s = NULL;
	unsigned i;

	HUB_DEBUG_DUMP;

	for (i = 0; i < USB_HUB_MAX; i++)
		if (!hubs[i].used) {
			s = &hubs[i];
			break;
		}

	if (NULL == s) {
		HUB_MSG("A hub was given and %d are held already; it is not "
			"driven", USB_HUB_MAX);
		return;
	}

	/* Clear what an earlier hub left */
	memset(&s->descriptor, 0, sizeof(s->descriptor));
	memset(s->conn, 0, sizeof(s->conn));
	s->num_ports = 0;
	s->gone = 0;
	s->dev = dev;
	s->used = 1;

	/* Its thread, the first time this entry is used */
	if (NULL == s->thread) {
		char name[16];

		s->done = ddekit_sem_init(0);
		s->given = ddekit_sem_init(0);
		if (NULL == s->done || NULL == s->given)
			panic("Initializing USB hub semaphores failed!");

		snprintf(name, sizeof(name), "hub_%u", s->num);
		s->thread = ddekit_thread_create(hub_thread, s, name);
		if (NULL == s->thread)
			panic("Initializing USB hub thread failed!");
	}

	HUB_PMSG(s, "given a hub");

	/* Let driver logic work */
	ddekit_sem_up(s->given);
}


/*===========================================================================*
 *    usb_driver_disconnect                                                  *
 *===========================================================================*/
static void
usb_driver_disconnect(struct ddekit_usb_dev * dev)
{
	unsigned i;

	HUB_DEBUG_DUMP;

	for (i = 0; i < USB_HUB_MAX; i++)
		if (hubs[i].used && hubs[i].dev == dev) {
			HUB_PMSG(&hubs[i], "withdrawn");
			hubs[i].gone = 1;
			return;
		}

	HUB_MSG("A device that is not one of the hubs was withdrawn");
}


/*===========================================================================*
 *    hub_submit                                                             *
 *===========================================================================*/
/* One request of one hub, under the lock on talking to xhci */
static int
hub_submit(hub_state * s, struct ddekit_usb_urb * urb)
{
	int r;

	ddekit_sem_down(bus);
	r = blocking_urb_submit(urb, s->done, URB_SUBMIT_CHECK_LEN);
	ddekit_sem_up(bus);

	return r;
}


/*===========================================================================*
 *    hub_news                                                               *
 *===========================================================================*/
/* News about a port, naming this hub, under the same lock */
static int
hub_news(hub_state * s, long type, int port_num)
{
	int r;

	/* A hub that has gone has no ports; xhci let go of what was behind
	 * it before it said so */
	if (s->gone)
		return EXIT_SUCCESS;

	ddekit_sem_down(bus);
	r = ddekit_usb_info(s->dev, type, (long)port_num);
	ddekit_sem_up(bus);

	return r;
}


/*===========================================================================*
 *    hub_power_on                                                           *
 *===========================================================================*/
/*
 * Power on every port and wait for the power to be good.
 *
 * A hub that switches power - ganged or per port - comes out of being
 * configured with its ports off, and nothing on them can be seen until the
 * host turns them on (USB 2.0, 11.11); one that does not switch has them on
 * already and takes the request as a no-op.  So the request goes to every
 * port either way, as Linux sends it (hub_power_on()), and the wait is the
 * hub's own figure.  This is also what keeps a dock's USB-A ports giving
 * current to a phone that is only charging: a port is powered whatever is
 * or is not enumerated on it, and nothing below turns it off.
 */
static int
hub_power_on(hub_state * s)
{
	hub_descriptor * d = &s->descriptor;
	unsigned wait;
	int port;

	for (port = 1; port <= s->num_ports; port++) {
		if (hub_port_feature(s, port, SET_FEATURE, PORT_POWER)) {
			HUB_PMSG(s, "Powering port%d failed", port);
			return EXIT_FAILURE;
		}
	}

	wait = 2 * (unsigned)d->bPwrOn2PwrGood;
	if (wait < USB_HUB_POWER_GOOD_MIN)
		wait = USB_HUB_POWER_GOOD_MIN;
	ddekit_thread_msleep(wait);

	return EXIT_SUCCESS;
}


/*===========================================================================*
 *    hub_other_changes                                                      *
 *===========================================================================*/
/*
 * The changes on a port that are not about connection: over-current, the
 * hub disabling the port, and a reset or suspend nobody asked for.  Each is
 * acknowledged - a change bit left standing stands for ever - and two are
 * acted on.
 *
 * Over-current is the one a phone on a dock's port causes.  The hub takes
 * the power off by itself and says so; when the condition clears it says
 * so again, and the port is powered once more after a rest.  This driver
 * used to read none of it, and a port that once drew too much stayed off.
 *
 * A port the hub disabled under a device that is still there (babble,
 * interference) is taken as the device having left and come back, which is
 * what Linux does ("disabled by hub (EMI?), re-enabling..."): its device
 * is reported gone and the connection handled anew.
 */
static int
hub_other_changes(hub_state * s, int port, hub_port_status * status)
{
	if (status->C_PORT_OVER_CURRENT) {
		if (hub_port_feature(s, port, CLEAR_FEATURE,
				C_PORT_OVER_CURRENT))
			return EXIT_FAILURE;
		HUB_PMSG(s, "Port%d over-current %s, power %s", port,
			status->PORT_OVER_CURRENT ? "now" : "cleared",
			status->PORT_POWER ? "on" : "off");
		if (!status->PORT_OVER_CURRENT && !status->PORT_POWER) {
			ddekit_thread_msleep(USB_HUB_OVERCURRENT_REST);
			if (hub_port_feature(s, port, SET_FEATURE, PORT_POWER))
				return EXIT_FAILURE;
			HUB_PMSG(s, "Port%d powered again", port);
		}
	}

	if (status->C_PORT_ENABLE) {
		if (hub_port_feature(s, port, CLEAR_FEATURE, C_PORT_ENABLE))
			return EXIT_FAILURE;
		if (HUB_PORT_CONN == s->conn[port] &&
				status->PORT_CONNECTION && !status->PORT_ENABLE &&
				!status->C_PORT_CONNECTION) {
			HUB_PMSG(s, "Port%d disabled by the hub; its device "
				"is taken to have reconnected", port);
			status->C_PORT_CONNECTION = 1;
		}
	}

	if (status->C_PORT_SUSPEND &&
			hub_port_feature(s, port, CLEAR_FEATURE, C_PORT_SUSPEND))
		return EXIT_FAILURE;

	if (status->C_PORT_RESET) {
		if (hub_port_feature(s, port, CLEAR_FEATURE, C_PORT_RESET))
			return EXIT_FAILURE;
		status->C_PORT_RESET = 0;
	}

	return EXIT_SUCCESS;
}


/*===========================================================================*
 *    hub_serve                                                              *
 *===========================================================================*/
/*
 * One hub, from being given it to its going.  Returns when the hub has been
 * withdrawn, or on an error this driver cannot get past - in which case the
 * hub is left alone until it is withdrawn.
 */
static void
hub_serve(hub_state * s)
{
	hub_port_status port_status;
	hub_descriptor * d;
	int port;

	HUB_DEBUG_DUMP;

	d = &(s->descriptor);

	if (hub_get_descriptor(s, d)) {
		if (!s->gone)
			HUB_PMSG(s, "Getting hub descriptor failed");
		return;
	}

	/* Check for sane number of ports... */
	if (d->bNbrPorts > USB_HUB_PORT_LIMIT) {
		HUB_PMSG(s, "Too many hub ports declared: %d", d->bNbrPorts);
		return;
	}

	/* ...and reassign */
	s->num_ports = (int)d->bNbrPorts;

	HUB_PMSG(s, "%d port(s), %s power switching, %s over-current "
		"protection, think time %u, power good in %u ms",
		s->num_ports,
		HUB_CHAR_POWER(d->wHubCharacteristics) == HUB_CHAR_POWER_GANGED ?
		"ganged" :
		HUB_CHAR_POWER(d->wHubCharacteristics) == HUB_CHAR_POWER_PORT ?
		"per-port" : "no",
		HUB_CHAR_OC(d->wHubCharacteristics) == 0 ? "global" :
		HUB_CHAR_OC(d->wHubCharacteristics) == 1 ? "per-port" : "no",
		HUB_CHAR_TTT(d->wHubCharacteristics),
		2 * (unsigned)d->bPwrOn2PwrGood);

	/* Initialize all available ports starting
	 * from 1, as defined by USB 2.0 document */
	if (hub_power_on(s)) {
		if (!s->gone)
			HUB_PMSG(s, "Powering the ports failed");
		return;
	}

	/*
	 * Connection polling loop
	 */
	for (;;) {
		for (port = 1; port <= s->num_ports; port++) {

			if (s->gone)
				return;

			/* Get port status */
			if (hub_get_port_status(s, port, &port_status)) {
				if (!s->gone)
					HUB_PMSG(s, "Reading port%d status "
						"failed", port);
				return;
			}

			if (hub_other_changes(s, port, &port_status)) {
				if (!s->gone)
					HUB_PMSG(s, "Port%d: acknowledging a "
						"change failed", port);
				return;
			}

			/*
			 * A port given up on is watched for its device
			 * leaving, and is usable again when it has.  It
			 * stays powered meanwhile.
			 */
			if (HUB_PORT_ERROR == s->conn[port]) {
				if (!port_status.C_PORT_CONNECTION)
					continue;
				if (hub_port_feature(s, port, CLEAR_FEATURE,
						C_PORT_CONNECTION))
					return;
				if (!port_status.PORT_CONNECTION) {
					HUB_PMSG(s, "Port%d: its device is gone; "
						"the port is in use again", port);
					s->conn[port] = HUB_PORT_DISCONN;
				}
				continue;
			}

			/* Resolve port changes */
			switch (hub_handle_change(s, port, &port_status)) {

				case HUB_CHANGE_NONE:
					break;

				case HUB_CHANGE_CONN:
					s->conn[port] = HUB_PORT_CONN;
					break;

				case HUB_CHANGE_DISCONN:
					s->conn[port] = HUB_PORT_DISCONN;
					break;

				case HUB_CHANGE_STATUS_ERR:
					if (s->gone)
						return;
					/*
					 * Disable the port and leave it be
					 * until its device goes.  Not power
					 * it off, as this did: on a dock's
					 * port that is a phone that stops
					 * charging because it would not
					 * enumerate, and the port then dead
					 * until the hub is unplugged.  Linux
					 * disables (hub_port_disable()).
					 */
					if (hub_port_feature(s, port,
							CLEAR_FEATURE,
							PORT_ENABLE)) {
						HUB_PMSG(s, "Disabling port%d "
							"failed", port);
						return;
					}
					s->conn[port] = HUB_PORT_ERROR;

					HUB_PMSG(s, "Port%d status ERROR; "
						"disabled until its device is "
						"unplugged (it stays powered)",
						port);

					break;

				case HUB_CHANGE_COM_ERR:
					if (s->gone)
						return;
					/* Serious error, hang */
					HUB_PMSG(s, "Handling port%d change "
						"failed", port);
					return;
			}
		}

		ddekit_thread_msleep(USB_HUB_POLLING_INTERVAL);
		HUB_DEBUG_MSG("Polling USB hub for status change");
	}
}


/*===========================================================================*
 *    hub_thread                                                             *
 *===========================================================================*/
static void
hub_thread(void * arg)
{
	hub_state * s = (hub_state *)arg;

	HUB_DEBUG_DUMP;

	for (;;) {
		/* Wait for a hub */
		ddekit_sem_down(s->given);

		hub_serve(s);

		/*
		 * The entry is another hub's only once this one has been
		 * withdrawn: until then it is the hub's, working or not.
		 */
		if (!s->gone)
			HUB_PMSG(s, "Hub driver error occurred; the hub is "
				"left alone until it is unplugged");
		while (!s->gone)
			ddekit_thread_msleep(USB_HUB_POLLING_INTERVAL);

		HUB_PMSG(s, "released");
		s->dev = NULL;
		s->used = 0;
	}
}


/*===========================================================================*
 *    hub_get_descriptor                                                     *
 *===========================================================================*/
static int
hub_get_descriptor(hub_state * s, hub_descriptor * descriptor)
{
	/* URB to be send */
	struct ddekit_usb_urb urb;

	/* Setup buffer to be attached */
	struct usb_ctrlrequest setup_buf;

	/* Control EP configuration */
	urb_ep_config ep_conf;

	HUB_DEBUG_DUMP;

	/* Initialize EP configuration */
	ep_conf.ep_num = 0;
	ep_conf.direction = DDEKIT_USB_IN;
	ep_conf.type = DDEKIT_USB_TRANSFER_CTL;
	ep_conf.max_packet_size = 0;
	ep_conf.interval = 0;

	/* Reset URB and assign given values */
	init_urb(&urb, s->dev, &ep_conf);

	/* Clear setup data */
	memset(&setup_buf, 0, sizeof(setup_buf));

	/* Class get hub descriptor request */
	setup_buf.bRequestType = 0xA0;
	setup_buf.bRequest = 0x06;
	setup_buf.wValue = USB_HUB_DESCRIPTOR_TYPE << 8;
	setup_buf.wIndex = 0x00;
	setup_buf.wLength = sizeof(*descriptor);

	/* Attach buffers to URB */
	attach_urb_data(&urb, URB_BUF_TYPE_SETUP,
			&setup_buf, sizeof(setup_buf));
	attach_urb_data(&urb, URB_BUF_TYPE_DATA,
			descriptor, sizeof(*descriptor));

	/* Send and wait for response */
	if (hub_submit(s, &urb)) {
		if (!s->gone)
			HUB_PMSG(s, "Submitting HUB URB failed");
		return EXIT_FAILURE;
	} else {
		HUB_DEBUG_MSG("HUB descriptor received");
		return EXIT_SUCCESS;
	}
}


/*===========================================================================*
 *    hub_port_feature                                                       *
 *===========================================================================*/
static int
hub_port_feature(hub_state * s, int port_num, class_code code,
	class_feature feature)
{
	/* URB to be send */
	struct ddekit_usb_urb urb;

	/* Setup buffer to be attached */
	struct usb_ctrlrequest setup_buf;

	/* Control EP configuration */
	urb_ep_config ep_conf;

	HUB_DEBUG_DUMP;

	/* TODO: Add more checks when needed */
	if (!((port_num <= s->num_ports) && (port_num > 0)))
		return EXIT_FAILURE;

	if (!((code == SET_FEATURE) || (code == CLEAR_FEATURE)))
		return EXIT_FAILURE;

	if (!((feature == PORT_RESET) || (feature == PORT_POWER) ||
		(feature == PORT_ENABLE) ||
		(feature == C_PORT_CONNECTION) || (feature == C_PORT_RESET) ||
		(feature == C_PORT_ENABLE) || (feature == C_PORT_SUSPEND) ||
		(feature == C_PORT_OVER_CURRENT)))
		return EXIT_FAILURE;

	/* Initialize EP configuration */
	ep_conf.ep_num = 0;
	ep_conf.direction = DDEKIT_USB_OUT;
	ep_conf.type = DDEKIT_USB_TRANSFER_CTL;
	ep_conf.max_packet_size = 0;
	ep_conf.interval = 0;

	/* Reset URB and assign given values */
	init_urb(&urb, s->dev, &ep_conf);

	/* Clear setup data */
	memset(&setup_buf, 0, sizeof(setup_buf));

	/* Standard get endpoint request */
	setup_buf.bRequestType = 0x23;
	setup_buf.bRequest = (u8_t)code;
	setup_buf.wValue = (u16_t)feature;
	setup_buf.wIndex = (u16_t)port_num;
	setup_buf.wLength = 0;

	/* Attach buffers to URB */
	attach_urb_data(&urb, URB_BUF_TYPE_SETUP,
			&setup_buf, sizeof(setup_buf));

	/* Send and wait for response */
	if (hub_submit(s, &urb)) {
		if (!s->gone)
			HUB_PMSG(s, "Submitting HUB URB failed");
		return EXIT_FAILURE;
	} else {
		HUB_DEBUG_MSG("PortFeature operation completed");
		return EXIT_SUCCESS;
	}
}


/*===========================================================================*
 *    hub_get_port_status                                                    *
 *===========================================================================*/
static int
hub_get_port_status(hub_state * s, int port_num, hub_port_status * p)
{
	/* URB to be send */
	struct ddekit_usb_urb urb;

	/* Setup buffer to be attached */
	struct usb_ctrlrequest setup_buf;

	/* Control EP configuration */
	urb_ep_config ep_conf;

	HUB_DEBUG_DUMP;

	if (!((port_num <= s->num_ports) && (port_num > 0)))
		return EXIT_FAILURE;

	/* Initialize EP configuration */
	ep_conf.ep_num = 0;
	ep_conf.direction = DDEKIT_USB_IN;
	ep_conf.type = DDEKIT_USB_TRANSFER_CTL;
	ep_conf.max_packet_size = 0;
	ep_conf.interval = 0;

	/* Reset URB and assign given values */
	init_urb(&urb, s->dev, &ep_conf);

	/* Clear setup data */
	memset(&setup_buf, 0, sizeof(setup_buf));

	/* Standard get endpoint request */
	setup_buf.bRequestType = 0xA3;
	setup_buf.bRequest = (u8_t)GET_STATUS;
	setup_buf.wValue = 0x00;
	setup_buf.wIndex = (u16_t)port_num;
	setup_buf.wLength = sizeof(*p);

	/* Attach buffers to URB */
	attach_urb_data(&urb, URB_BUF_TYPE_SETUP,
			&setup_buf, sizeof(setup_buf));
	attach_urb_data(&urb, URB_BUF_TYPE_DATA,
			p, sizeof(*p));

	/* Send and wait for response */
	if (hub_submit(s, &urb)) {
		if (!s->gone)
			HUB_PMSG(s, "Submitting HUB URB failed");
		return EXIT_FAILURE;
	} else {
		HUB_DEBUG_MSG("Port%d status:      ", port_num);
		HUB_DEBUG_MSG("PORT_CONNECTION   %01X", p->PORT_CONNECTION);
		HUB_DEBUG_MSG("PORT_ENABLE       %01X", p->PORT_ENABLE);
		HUB_DEBUG_MSG("PORT_POWER        %01X", p->PORT_POWER);
		HUB_DEBUG_MSG("C_PORT_CONNECTION %01X", p->C_PORT_CONNECTION);
		HUB_DEBUG_MSG("C_PORT_ENABLE     %01X", p->C_PORT_ENABLE);
		return EXIT_SUCCESS;
	}
}


/*===========================================================================*
 *    hub_handle_change                                                      *
 *===========================================================================*/
static port_change
hub_handle_change(hub_state * s, int port_num, hub_port_status * status)
{
	port_conn * c;

	HUB_DEBUG_DUMP;

	/* Possible combinations: */
	/* Change = status->C_PORT_CONNECTION	(hub connection change bit)
	 * Local = s->conn[port_num]		(local connection status)
	 * Remote = status->PORT_CONNECTION	(hub connection status) */
	/*
	Case	Change	Local	Remote	Description
	1.	1	1	1	Polling mismatch (quick disconn-conn)
	2.	1	1	0	Just disconnected
	3.	1	0	1	Just connected
	4.	1	0	0	Polling mismatch (quick conn-disconn)
	5.	0	1	1	Still connected
	6.	0	1	0	Gone, and the hub did not say so
	7.	0	0	1	There, and nobody was told
	8.	0	0	0	Still disconnected
	*/
	/*
	 * Cases 6 and 7 were "serious ERROR", and 7 then powered the port
	 * off for good.  But 7 is exactly what a driver that starts while
	 * devices are plugged in sees, when its predecessor had already
	 * acknowledged their connection - a restarted hub driver, or a hub
	 * given to this driver again - and 6 is the mirror of it.  What the
	 * hub says is the truth, and the driver follows it: 7 is handled as
	 * a connection, 6 as a disconnection.
	 */

	/* Reassign for code cleanliness */
	c = s->conn;

	/* Resolve combination */
	if (status->C_PORT_CONNECTION) {

		/* C_PORT_CONNECTION was set, so clear change bit
		 * to allow further polling */
		if (hub_port_feature(s, port_num, CLEAR_FEATURE,
					C_PORT_CONNECTION)) {
			if (!s->gone)
				HUB_PMSG(s, "Clearing port%d change bit "
					"failed", port_num);
			return HUB_CHANGE_COM_ERR;
		}

		if (HUB_PORT_CONN == c[port_num]) {
			if (status->PORT_CONNECTION) {

				/*
				 * 1
				 */
				/* Make hub disconnect and connect again */
				if (hub_handle_disconnection(s, port_num))
					return HUB_CHANGE_STATUS_ERR;
				switch (hub_handle_connection(s, port_num,
						status)) {
				case EXIT_SUCCESS:	return HUB_CHANGE_CONN;
				case HUB_CONN_GONE:	return HUB_CHANGE_DISCONN;
				default:		return HUB_CHANGE_STATUS_ERR;
				}

			} else {

				/*
				 * 2
				 */
				/* Handle disconnection */
				if (hub_handle_disconnection(s, port_num))
					return HUB_CHANGE_STATUS_ERR;
				else
					return HUB_CHANGE_DISCONN;

			}
		} else if (HUB_PORT_DISCONN == c[port_num]) {
			if (status->PORT_CONNECTION) {

				/*
				 * 3
				 */
				/* Handle connection */
				switch (hub_handle_connection(s, port_num,
						status)) {
				case EXIT_SUCCESS:	return HUB_CHANGE_CONN;
				case HUB_CONN_GONE:	return HUB_CHANGE_DISCONN;
				default:		return HUB_CHANGE_STATUS_ERR;
				}

			} else {

				/*
				 * 4
				 */
				/* Since we were disconnected before and
				 * are disconnected now, additional handling
				 * may be ignored */
				return HUB_CHANGE_NONE;

			}
		}
	} else {
		if (HUB_PORT_CONN == c[port_num]) {
			if (status->PORT_CONNECTION) {

				/*
				 * 5
				 */
				/* Connected (nothing changed) */
				return HUB_CHANGE_NONE;

			} else {

				/*
				 * 6
				 */
				HUB_PMSG(s, "Port%d: its device is gone with no "
					"change reported", port_num);
				if (hub_handle_disconnection(s, port_num))
					return HUB_CHANGE_STATUS_ERR;
				return HUB_CHANGE_DISCONN;

			}
		} else if (HUB_PORT_DISCONN == c[port_num]) {
			if (status->PORT_CONNECTION) {

				/*
				 * 7
				 */
				HUB_PMSG(s, "Port%d: a device that nobody was "
					"told about", port_num);
				switch (hub_handle_connection(s, port_num,
						status)) {
				case EXIT_SUCCESS:	return HUB_CHANGE_CONN;
				case HUB_CONN_GONE:	return HUB_CHANGE_DISCONN;
				default:		return HUB_CHANGE_STATUS_ERR;
				}

			} else {

				/*
				 * 8
				 */
				/* Disconnected (nothing changed) */
				return HUB_CHANGE_NONE;

			}
		}
	}

	return HUB_CHANGE_COM_ERR;
}


/*===========================================================================*
 *    hub_handle_connection                                                  *
 *===========================================================================*/
/*
 * Wait for a connection to hold still before doing anything about it.
 *
 * USB 2.0 asks for it (7.1.7.3, TATTDB: a hundred milliseconds with no
 * connection change), and this driver skipped it: it saw a port with a
 * device on it and reset the port in the same breath.  A device that is
 * still settling - one that has just had its power cycled, or one that
 * was in the middle of something when the whole hub was reset underneath
 * it, which is what a restart of the host driver does to it - then
 * disconnects and reconnects during the reset, the port comes out of the
 * reset not enabled, and the driver blocks the port until the hub is
 * detached.  On the board the hub is soldered on.
 *
 * Every connection change seen here is cleared, so that the reset that
 * follows starts from a quiet port; if the device is not there any more
 * at the end, that is not an error but a disconnection.
 *
 * The waits here and below are the thread's own (ddekit_thread_msleep()),
 * not the process's: they were nanosleep(), which puts every hub's thread
 * to sleep along with the one that is waiting.
 */
static int
hub_port_debounce(hub_state * s, int port_num, hub_port_status * status)
{
	int stable = 0, waited = 0;

	while (waited < USB_HUB_DEBOUNCE_LIMIT) {
		ddekit_thread_msleep(USB_HUB_DEBOUNCE_STEP);
		waited += USB_HUB_DEBOUNCE_STEP;

		if (hub_get_port_status(s, port_num, status)) {
			if (!s->gone)
				HUB_PMSG(s, "Reading port%d status failed",
					port_num);
			return EXIT_FAILURE;
		}

		if (status->C_PORT_CONNECTION) {
			if (hub_port_feature(s, port_num, CLEAR_FEATURE,
						C_PORT_CONNECTION)) {
				if (!s->gone)
					HUB_PMSG(s, "Clearing port%d change "
						"bit failed", port_num);
				return EXIT_FAILURE;
			}
			HUB_PMSG(s, "Port%d connection changed while settling "
				"(status %08X)", port_num, *(uint32_t *)status);
			stable = 0;
			continue;
		}

		stable += USB_HUB_DEBOUNCE_STEP;
		if (stable >= USB_HUB_DEBOUNCE_STABLE)
			return status->PORT_CONNECTION ? EXIT_SUCCESS :
				HUB_CONN_GONE;
	}

	HUB_PMSG(s, "Port%d connection did not settle", port_num);
	return EXIT_FAILURE;
}

/*
 * Reset a port with a settled connection on it, until it is enabled, and
 * say at what speed: EXIT_SUCCESS, HUB_CONN_GONE or EXIT_FAILURE.
 */
static int
hub_port_reset(hub_state * s, int port_num, hub_port_status * status,
	long * speed)
{
	int reset_tries, reset_rounds;
	long port_speed;

	/*
	 * The reset, and again if the device moved during it.
	 *
	 * A connection change reported after the reset means the device
	 * dropped off the bus and came back while being reset; the port is
	 * then not enabled and nothing below can proceed.  The specification
	 * treats that as a reset that did not take, to be repeated, not as
	 * a bad port.  The old code called the port unusable for ever.
	 */
	for (reset_rounds = 0; ; reset_rounds++) {

	/* Start reset signaling for this port */
	if (hub_port_feature(s, port_num, SET_FEATURE, PORT_RESET)) {
		HUB_PMSG(s, "Resetting port%d failed", port_num);
		return EXIT_FAILURE;
	}

	reset_tries = 0;
	status->C_PORT_RESET = 0;

	/* Wait for reset completion */
	while (!status->C_PORT_RESET) {
		/* To avoid endless loop */
		if (reset_tries >= USB_HUB_MAX_TRIES) {
			HUB_PMSG(s, "Port%d reset took too long", port_num);
			return EXIT_FAILURE;
		}

		/* Get port status again */
		if (hub_get_port_status(s, port_num, status)) {
			HUB_PMSG(s, "Reading port%d status failed", port_num);
			return EXIT_FAILURE;
		}

		reset_tries++;

		ddekit_thread_msleep(USB_HUB_RESET_DELAY);
	}

	/* Reset completed */
	HUB_DEBUG_MSG("Port%d reset complete", port_num);

	/* Dump full status for analysis (high-speed, ...) */
	HUB_DEBUG_MSG("C_PORT_CONNECTION   %1X", status->C_PORT_CONNECTION  );
	HUB_DEBUG_MSG("C_PORT_ENABLE       %1X", status->C_PORT_ENABLE      );
	HUB_DEBUG_MSG("C_PORT_OVER_CURRENT %1X", status->C_PORT_OVER_CURRENT);
	HUB_DEBUG_MSG("C_PORT_RESET        %1X", status->C_PORT_RESET       );
	HUB_DEBUG_MSG("C_PORT_SUSPEND      %1X", status->C_PORT_SUSPEND     );
	HUB_DEBUG_MSG("PORT_CONNECTION     %1X", status->PORT_CONNECTION    );
	HUB_DEBUG_MSG("PORT_ENABLE         %1X", status->PORT_ENABLE        );
	HUB_DEBUG_MSG("PORT_HIGH_SPEED     %1X", status->PORT_HIGH_SPEED    );
	HUB_DEBUG_MSG("PORT_INDICATOR      %1X", status->PORT_INDICATOR     );
	HUB_DEBUG_MSG("PORT_LOW_SPEED      %1X", status->PORT_LOW_SPEED     );
	HUB_DEBUG_MSG("PORT_OVER_CURRENT   %1X", status->PORT_OVER_CURRENT  );
	HUB_DEBUG_MSG("PORT_POWER          %1X", status->PORT_POWER         );
	HUB_DEBUG_MSG("PORT_RESET          %1X", status->PORT_RESET         );
	HUB_DEBUG_MSG("PORT_SUSPEND        %1X", status->PORT_SUSPEND       );
	HUB_DEBUG_MSG("PORT_TEST           %1X", status->PORT_TEST          );

	/* Clear reset change bit for further devices */
	if (hub_port_feature(s, port_num, CLEAR_FEATURE, C_PORT_RESET)) {
		HUB_PMSG(s, "Clearing port%d reset bit failed", port_num);
		return EXIT_FAILURE;
	}

	/* Nothing moved: the reset took. */
	if (!status->C_PORT_CONNECTION)
		break;

	/*
	 * The device dropped off and came back while being reset - the
	 * board showed it as "01 01 11 00": reset complete, connection
	 * changed, port not enabled.  Clear the change and try again, or
	 * give up if it keeps happening or the device is gone.
	 */
	if (hub_port_feature(s, port_num, CLEAR_FEATURE, C_PORT_CONNECTION)) {
		HUB_PMSG(s, "Clearing port%d change bit failed", port_num);
		return EXIT_FAILURE;
	}
	if (!status->PORT_CONNECTION) {
		HUB_PMSG(s, "Port%d: device gone during reset", port_num);
		return HUB_CONN_GONE;
	}
	if (reset_rounds + 1 >= USB_HUB_RESET_ROUNDS) {
		HUB_PMSG(s, "Port%d: connection kept changing during reset",
			port_num);
		return EXIT_FAILURE;
	}
	HUB_PMSG(s, "Port%d: connection changed during reset (status %08X); "
		"resetting again", port_num, *(uint32_t *)status);
	ddekit_thread_msleep(USB_HUB_RESET_DELAY);

	}	/* for (reset_rounds) */

	/*
	 * A port that has just finished reset is not usable yet: the
	 * specification gives the hub a recovery time (TRSTRCY, 10 ms)
	 * before the port is enabled, and the reset-complete bit is set at
	 * the start of it rather than at the end.  Reading the status once
	 * more straight away therefore catches the port reset-complete and
	 * not yet enabled, and this driver called that "unexpectedly
	 * unavailable" and gave up on the port for good.
	 *
	 * It was not seen until now because it needs a controller quick
	 * enough to get the answer back inside that window: on the board
	 * the same build failed every time with logging off and worked
	 * every time with logging on, which is what a race looks like when
	 * the only thing that changed was how long a printf takes.
	 */
	reset_tries = 0;
	while (status->PORT_CONNECTION && !status->PORT_ENABLE) {
		if (reset_tries >= USB_HUB_MAX_TRIES) {
			HUB_PMSG(s, "Port%d did not enable after reset",
				port_num);
			return EXIT_FAILURE;
		}

		ddekit_thread_msleep(USB_HUB_RESET_DELAY);

		if (hub_get_port_status(s, port_num, status)) {
			HUB_PMSG(s, "Reading port%d status failed", port_num);
			return EXIT_FAILURE;
		}

		reset_tries++;
	}

	/* Should never happen */
	if (!status->PORT_CONNECTION || !status->PORT_ENABLE) {
		HUB_PMSG(s, "Port%d unexpectedly unavailable", port_num);
		return EXIT_FAILURE;
	}

	/* Determine port speed from status bits */
	if (status->PORT_LOW_SPEED) {
		if (status->PORT_HIGH_SPEED) {
			HUB_PMSG(s, "Port%d has invalid speed flags",
				port_num);
			return EXIT_FAILURE;
		} else
			port_speed = (long)DDEKIT_HUB_PORT_LS_CONN;
	} else {
		if (status->PORT_HIGH_SPEED)
			port_speed = (long)DDEKIT_HUB_PORT_HS_CONN;
		else
			port_speed = (long)DDEKIT_HUB_PORT_FS_CONN;
	}

	*speed = port_speed;
	return EXIT_SUCCESS;
}

/*
 * A device on a port: let it settle, reset the port, tell the host
 * controller - and if the controller could not address the device, reset
 * the port and tell it again.
 *
 * The retry is Linux's (hub_port_connect(): PORT_INIT_TRIES, a fresh reset
 * each time), and it is here because the board needed it: the dock's
 * RTL8152, three hubs below the root port, answered its first Address
 * Device with a transaction error on every bring-up, though its port was
 * enabled at high speed - the hubs, the receiver and the card reader on
 * the same run took their addresses the first time.  A device that never
 * takes one is given up on as before: its port is disabled and stays
 * powered.
 */
static int
hub_handle_connection(hub_state * s, int port_num, hub_port_status * status)
{
	int attempt, r;
	long port_speed;

	HUB_DEBUG_DUMP;

	HUB_PMSG(s, "Device connected to port%d", port_num);

	/* This should never happen if power-off works as intended */
	if (status->C_PORT_RESET) {
		HUB_PMSG(s, "Unexpected reset state for port%d", port_num);
		return EXIT_FAILURE;
	}

	/* Let the connection settle before resetting it */
	if ((r = hub_port_debounce(s, port_num, status)) != EXIT_SUCCESS) {
		if (r == HUB_CONN_GONE)
			HUB_PMSG(s, "Port%d: device gone before reset",
				port_num);
		return r;
	}

	for (attempt = 1; ; attempt++) {
		if ((r = hub_port_reset(s, port_num, status, &port_speed)) !=
				EXIT_SUCCESS)
			return r;

		/* Signal to HCD that port has device connected at given speed */
		if ((r = hub_news(s, port_speed, port_num)) == EXIT_SUCCESS)
			return EXIT_SUCCESS;
		if (s->gone)
			return EXIT_FAILURE;
		if (attempt >= USB_HUB_ENUM_TRIES) {
			HUB_PMSG(s, "Port%d: the host controller could not "
				"address its device %d times; given up",
				port_num, attempt);
			return EXIT_FAILURE;
		}
		HUB_PMSG(s, "Port%d: the host controller could not address "
			"its device (%d); resetting again", port_num, r);
		ddekit_thread_msleep(USB_HUB_RESET_DELAY);
		if (hub_get_port_status(s, port_num, status))
			return EXIT_FAILURE;
		if (!status->PORT_CONNECTION) {
			HUB_PMSG(s, "Port%d: device gone", port_num);
			return HUB_CONN_GONE;
		}
	}
}


/*===========================================================================*
 *    hub_handle_disconnection                                               *
 *===========================================================================*/
static int
hub_handle_disconnection(hub_state * s, int port_num)
{
	HUB_DEBUG_DUMP;

	HUB_PMSG(s, "Device disconnected from port%d", port_num);

	return hub_news(s, (long)DDEKIT_HUB_PORT_DISCONN, port_num);
}
