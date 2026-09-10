/*
 * The kernel, for a driver that no longer polls.
 *
 * The driver stopped spinning on the event ring and started waiting for
 * the interrupt, which is what made it fast; that means the stand has to
 * be able to be the kernel for it - deliver a notification when the
 * modelled controller raises its line, and an alarm when the deadline
 * passes.  Without this half the stand could only test the path the
 * driver no longer takes.
 *
 * Included by ringtest.c rather than compiled separately, because it is
 * part of the same fixture and shares its state.
 */

static long alarm_ticks;
static unsigned n_spurious;
static unsigned irq_enables, deferred;

void
read_frclock_64(u64_t *t)
{
	*t = fr_now;
}

u64_t
delta_frclock_64(u64_t base, u64_t now)
{
	return now - base;
}

u64_t
frclock_64_to_micros(u64_t delta)
{
	return delta;
}

long
micros_to_ticks(unsigned long usec)
{
	return (long)(usec / 1000) + 1;	/* a tick is a millisecond here */
}

int
sys_setalarm(long ticks, int abs)
{
	alarm_ticks = ticks;
	return OK;
}

int
sys_irqenable(int *hook)
{
	irq_enables++;
	return OK;
}

/*
 * Wait for the controller or for the deadline, whichever comes first.
 *
 * The modelled controller settles in a handful of steps, so a run of
 * steps with nothing raised is the deadline having passed rather than a
 * slow device.  On the alarm the clock is advanced by the whole interval
 * that was asked for, because that is what the driver believes happened
 * and what its own deadline arithmetic is entitled to assume.
 */
#define SETTLE_STEPS	64

int
sef_receive_status(endpoint_t src, message *m, int *status)
{
	unsigned i;

	memset(m, 0, sizeof(*m));
	*status = 1;			/* a notification */

	for (i = 0; i < SETTLE_STEPS; i++) {
		fr_now += 10;
		model_step();

		if (xhci_rd(xhci.regs, xhci.rtsoff + XHCI_IR(0) +
		    XHCI_IR_IMAN) & XHCI_IMAN_IP) {
			if (!model_event_pending(xhci.event_phys +
			    xhci.event_deq * XHCI_TRB_SIZE,
			    xhci.event_cycle) && n_spurious++ < 4)
				fprintf(stderr, "  stand: the line was "
				    "raised with nothing on the ring\n");

			m->m_source = HARDWARE;
			n_hw++;
			return OK;
		}
	}

	fr_now += (unsigned long)alarm_ticks * 1000;
	n_clock++;
	m->m_source = CLOCK;
	return OK;
}

/*
 * A request that arrives while the driver is inside a transfer.  The
 * driver puts it aside; the stand only counts them, because what happens
 * to a deferred request is the URB layer's business and not the ring's.
 */
void
xhci_defer(message *m, int ipc_status)
{
	deferred++;
}

/*
 * The client half, as far as the event ring is concerned: an outstanding
 * transfer is a TRB address somebody is waiting to hear about.  Claiming
 * one is what the URB server does when a client's transfer finishes.
 */
#define MAX_OUTSTANDING	64

static phys_bytes claim_addr[MAX_OUTSTANDING];
static int claim_seen[MAX_OUTSTANDING];
static unsigned nclaims;
static unsigned claims_matched, claims_repeated;

static void
claims_reset(void)
{
	memset(claim_seen, 0, sizeof(claim_seen));
	nclaims = 0;
}

int
xhci_urb_transfer_event(const struct xhci_trb *ev)
{
	unsigned i;

	for (i = 0; i < nclaims; i++) {
		if ((uint32_t)claim_addr[i] != ev->p0)
			continue;
		if (claim_seen[i]) {
			claims_repeated++;
			return 1;
		}
		claim_seen[i] = 1;
		claims_matched++;
		return 1;
	}
	return 0;			/* the driver's own, not a client's */
}

/*===========================================================================*
 *    transfers that are outstanding at the same time                        *
 *===========================================================================*/
/*
 * This is the check the asynchronous path exists for, and the one that
 * would have found where it stopped.
 *
 * On the board, two clients' transfers interleaved and both stopped:
 * "a 4-byte transfer for 65563 did not complete in time; IMAN pending 0,
 * USBSTS event 0" and the same for the other.  No events at all and
 * nothing raised - which reads as the controller having gone quiet, and
 * is in fact the opposite: the events had been taken off the ring and
 * never acknowledged.  Draining took a shortcut past event_done() for any
 * event a client claimed, so the dequeue pointer the controller reads
 * never moved; two hundred and fifty-five events later the ring was full
 * and the controller stopped writing events for everybody.
 *
 * Four hundred events go through here, which is more than the ring holds,
 * so a driver that fails to acknowledge cannot get to the end.
 */
static void
test_outstanding(void)
{
	static uint8_t a[64], b[64];
	struct xhci_ep *ina, *inb;
	unsigned round, i;
	uint32_t erdp, want;

	section("two transfers outstanding at once");

	check(bring_up(2) == OK, "the controller starts");
	model_control_handler(device_control);
	model_attach(0, 3);
	model_attach(1, 3);
	xhci_port_reset(0);
	xhci_port_reset(1);
	check(xhci_device_attach(0, &dev0) == OK, "the first enumerates");
	check(xhci_device_attach(1, &dev1) == OK, "the second enumerates");

	ina = xhci_device_ep(&dev0, 1, 1);
	inb = xhci_device_ep(&dev1, 1, 1);

	for (i = 0; i < sizeof(a); i++) {
		a[i] = (uint8_t)(0xa0 + (i & 0xf));
		b[i] = (uint8_t)(0xb0 + (i & 0xf));
	}

	claims_matched = claims_repeated = 0;

	for (round = 0; round < 200; round++) {
		claims_reset();

		model_ep_supply(dev0.slot, ina->dci, a, sizeof(a));
		model_ep_supply(dev1.slot, inb->dci, b, sizeof(b));

		/*
		 * Both are put on their rings and announced before either
		 * is waited for.  That is the whole difference from the
		 * synchronous path, and the only way the driver can be
		 * faster than one transfer per scheduler quantum.
		 */
		claim_addr[0] = xhci_transfer_start(&dev0, ina, sizeof(a));
		claim_addr[1] = xhci_transfer_start(&dev1, inb, sizeof(b));
		nclaims = 2;

		if (claim_addr[0] == 0 || claim_addr[1] == 0)
			break;

		xhci_events_drain(2000, NULL, XHCI_TRB_TRANSFER_EVENT);

		if (!claim_seen[0] || !claim_seen[1])
			break;

		/*
		 * What the controller wrote is not visible until it is asked
		 * for.  On the board this invalidate lives in the URB
		 * server's completion path; here it is the client's half of
		 * the hand-over, done where the client would do it.
		 */
		xhci_cache(CACHE_INVALIDATE, (void *)dev0.buf, XHCI_DEV_BUF,
		    "a transfer buffer");
		xhci_cache(CACHE_INVALIDATE, (void *)dev1.buf, XHCI_DEV_BUF,
		    "a transfer buffer");

		if (memcmp((void *)dev0.buf, a, sizeof(a)) != 0)
			break;
		if (memcmp((void *)dev1.buf, b, sizeof(b)) != 0)
			break;
	}

	check(round == 200, "%u of 200 interleaved pairs completed", round);
	check(claims_matched == 400, "%u of 400 events reached their client",
	    claims_matched);
	check(claims_repeated == 0, "and none of them arrived twice");

	/*
	 * The invariant underneath all of that, checked directly rather
	 * than through its consequences: the controller has been told how
	 * far the driver has read.  An event taken off the ring and not
	 * acknowledged is invisible until the ring fills, and then it is a
	 * silence rather than an error.
	 */
	erdp = xhci_rd(xhci.regs, xhci.rtsoff + XHCI_IR(0) + XHCI_IR_ERDP) &
	    ~0xfu;
	want = (uint32_t)(xhci.event_phys + xhci.event_deq * XHCI_TRB_SIZE);
	check(erdp == want, "the dequeue pointer the controller reads is "
	    "0x%08x, where the driver has read to", erdp);

	check(model_stats.event_ring_full == 0,
	    "the event ring never filled");
	check(model_complaint() == NULL, "the model saw nothing wrong: %s",
	    model_complaint() ? model_complaint() : "nothing");

	/* And it happened on the interrupt, not on a poll loop. */
	check(irq_enables > 0, "the line was armed %u time(s)", irq_enables);
}
