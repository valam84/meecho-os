/*
 * The WCN3990's hardware side, as the wcn service drives it: the supplies
 * and the reference clock through qcom_rpm, the copy engines' window, the
 * DMA memory, the twelve lines, and HTC over it all
 * (port/clover/hw-wifi.md, sections 4-6).
 *
 * The DMA memory is one contiguous block (alloc_contig: cacheable RAM,
 * maintained by hand - wcn_ce.h says how).  The engines reach it through
 * the apps SMMU (SID 0x1a00/0x1a01), which must map it at its physical
 * address before hw_start(): that route is the shared SMMU code's, not
 * this file's.
 */

#include "wcn.h"

#include <minix/cachectl.h>
#include <minix/ds.h>
#include <minix/qcom_rpm.h>
#include <minix/vm.h>

#include "smmu_route.h"

#include <sys/mman.h>

#include <stdlib.h>
#include <string.h>

#define ARENA		(8u << 20)
#define RXQ		256		/* messages kept per endpoint */
#define RXQ_MAX		2048

struct htc htc;
int wmi_ep = -1, htt_ep = -1;

static volatile uint8_t *regs;
static struct wcn_mem mem;
static uint8_t *arena;
static phys_bytes arena_pa;
static struct wcn_ce ce;
static void **ce_ctx;
static int hooks[WCN_CE_COUNT];
static int started;

/*
 * The apps SMMU (shared with the IPA; hw-wifi.md, section 6): SMR 1 and
 * bank 1 by agreement, ASID 2.  The DMA memory appears to the engines at
 * IOVA_BASE, under 4 GiB, whatever its physical address: no field the
 * firmware has will meet an address too wide.  Until the route is on the
 * engines must not start - a stream without a route faults
 * (sCR0.USFCFG = 1).
 */
#define WCN_SMR		1
#define WCN_CB		1
#define WCN_ASID	2
#define IOVA_BASE	0x20000000u

static struct smmu_route route;
static volatile uint8_t *smmu;
static int routed;

static int
smmu_up(void)
{
	int r;

	if (routed)
		return OK;
	if (smmu == NULL && (smmu = vm_map_phys(SELF,
	    (void *)(phys_bytes)dt.smmu_pa, (size_t)dt.smmu_sz)) ==
	    MAP_FAILED) {
		smmu = NULL;
		log_warn(&wlog, "cannot map the SMMU; not granted by RS?\n");
		return EPERM;
	}
	route.base = smmu;
	route.size = (size_t)dt.smmu_sz;
	route.log = &wlog;
	route.smr = WCN_SMR;
	route.cb = WCN_CB;
	route.asid = WCN_ASID;
	route.ias = 32;
	route.check = 0;		/* reading a bank stops a core here */
	if ((r = smmu_route_init(&route)) != OK) {
		log_warn(&wlog, "SMMU: %d\n", r);
		return r;
	}
	if ((r = smmu_route_map(&route, IOVA_BASE, arena_pa, ARENA, 1)) !=
	    OK) {
		log_warn(&wlog, "SMMU map: %d\n", r);
		return r;
	}
	log_info(&wlog, "SMMU: bank %u set, routing the streams now\n", WCN_CB);
	if ((r = smmu_route_on(&route, dt.sid, dt.nsid)) != OK) {
		log_warn(&wlog, "SMMU route: %d\n", r);
		return r;
	}
	routed = 1;
	log_info(&wlog, "SMMU: streams %x %x through bank %u, %x+%x at "
	    "%lx\n", dt.sid[0], dt.nsid > 1 ? dt.sid[1] : 0, WCN_CB, IOVA_BASE,
	    ARENA, (unsigned long)arena_pa);
	return OK;
}

void
hw_smmu_report(void)
{

	if (routed)
		smmu_route_report(&route);
}

/* Receive buffers: their address for the engine and their memory. */
struct rxbuf {
	uint8_t *va;
	uint64_t da;
	size_t len;
	unsigned slot;		/* send buffers: which of the 256 */
};

/* Received messages, per endpoint, until someone waits for them. */
static struct {
	uint8_t data[RXQ][RXQ_MAX];
	size_t len[RXQ];
	unsigned head, tail;
} *rxq[HTC_MAX_EP];

/*
 * The pure layers (wcn_ce.c, wcn_htc.c) are built without _SYSTEM: their
 * error codes are positive, a service's negative.  Every code that crosses
 * into the service goes through here - comparing a pure EAGAIN with the
 * service's EAGAIN otherwise fails, and a positive code reaches the
 * caller of an ioctl as a "success" with errno 0 (seen on the tablet).
 */
static int
own(int r)
{

	return r > 0 ? -r : r;
}

/* ---- the copy engines' operations ---- */

static uint32_t
ce_rd(void *arg, uint32_t off)
{

	(void)arg;
	return *(volatile uint32_t *)(volatile void *)(regs + off);
}

static void
ce_wr(void *arg, uint32_t off, uint32_t v)
{

	(void)arg;
	*(volatile uint32_t *)(volatile void *)(regs + off) = v;
}

static void
ce_clean(void *arg, void *va, size_t len)
{

	(void)arg;
	(void)sys_cachectl(CACHE_CLEAN, va, len);
}

static void
ce_inval(void *arg, void *va, size_t len)
{

	(void)arg;
	(void)sys_cachectl(CACHE_INVALIDATE, va, len);
}

static void
ce_flush(void *arg, void *va, size_t len)
{

	(void)arg;
	(void)sys_cachectl(CACHE_CLEAN_INVALIDATE, va, len);
}

static const struct wcn_ce_ops ce_ops = {
	ce_rd, ce_wr, ce_clean, ce_inval, ce_flush, NULL
};

/* ---- supplies ---- */

static int
rpm(uint32_t type, uint32_t id, uint32_t key, uint32_t value)
{
	static endpoint_t ep = NONE;
	message m;
	int r;

	if (ep == NONE && ds_retrieve_label_endpt("qcom_rpm", &ep) != OK) {
		ep = NONE;
		return ENODEV;
	}
	memset(&m, 0, sizeof(m));
	m.m_type = QRPM_SET;
	m.m_u32.data[QRPM_TYPE] = type;
	m.m_u32.data[QRPM_ID] = id;
	m.m_u32.data[QRPM_KEY] = key;
	m.m_u32.data[QRPM_VALUE] = value;
	if ((r = ipc_sendrec(ep, &m)) != OK) {
		ep = NONE;
		return r;
	}
	return m.m_type;
}

/*
 * Linux's ath10k_hw_power_on(): the four supplies and the reference
 * clock.  L6, L9 and L19 are on from the boot loader (shared with the
 * Bluetooth of the same chip); L5 is not.
 */
int
hw_power(int on)
{
	int r;

	if (on && (r = rpm(QRPM_RES_LDOA, 5, QRPM_KEY_UV, 525000)) != OK) {
		log_warn(&wlog, "L5 at 525 mV: %d\n", r);
		return r;
	}
	if ((r = rpm(QRPM_RES_LDOA, 5, QRPM_KEY_ENABLE, on ? 1 : 0)) != OK) {
		log_warn(&wlog, "L5 %s: %d\n", on ? "on" : "off", r);
		return r;
	}
	if ((r = rpm(QRPM_RES_CLKA, 4, QRPM_KEY_PIN_ENABLE, on ? 1 : 0)) !=
	    OK) {
		log_warn(&wlog, "rf_clk1 %s: %d\n", on ? "on" : "off", r);
		return r;
	}
	log_info(&wlog, "supplies %s (L5, rf_clk1)\n", on ? "on" : "off");
	return OK;
}

/* ---- receive ---- */

static void
deliver(unsigned eid, const uint8_t *msg, size_t len)
{
	unsigned t;

	/* HTT is handled as it comes: the data path. */
	if (htt_ep >= 0 && eid == (unsigned)htt_ep) {
		data_htt(msg, len);
		return;
	}

	if (eid >= HTC_MAX_EP || rxq[eid] == NULL)
		return;
	if (rxq[eid]->tail - rxq[eid]->head >= RXQ || len > RXQ_MAX) {
		log_warn(&wlog, "endpoint %u: message of %zu dropped\n", eid,
		    len);
		return;
	}
	t = rxq[eid]->tail++ % RXQ;
	memcpy(rxq[eid]->data[t], msg, len);
	rxq[eid]->len[t] = len;
}

/*
 * Buffers waiting to go back: a receive line is handed to the engine only
 * when none of its descriptors is the engine's (wcn_ce.h), so a buffer
 * taken from the middle of a line waits here until the line is whole.
 */
#define SPARE	512
static struct rxbuf *spare[WCN_CE_COUNT][SPARE];
static unsigned nspare[WCN_CE_COUNT];

static void
repost(unsigned id, struct rxbuf *b)
{
	unsigned i, k;

	if (b != NULL) {
		if (nspare[id] >= SPARE) {
			log_warn(&wlog, "ce%u: a buffer lost\n", id);
			free(b);
		} else
			spare[id][nspare[id]++] = b;
	}
	for (i = 0; i < nspare[id]; i++)
		if (wcn_ce_rx_post(&ce, id, spare[id][i]->va, spare[id][i]->da,
		    spare[id][i]->len, spare[id][i]) != 0)
			break;
	for (k = 0; i + k < nspare[id]; k++)
		spare[id][k] = spare[id][i + k];
	nspare[id] -= i;
	wcn_ce_rx_commit(&ce, id);
}

/* Take what engine "id" received; give its buffers back. */
static void
service_rx(unsigned id)
{
	struct rxbuf *b;
	const uint8_t *msg;
	size_t len, mlen;
	unsigned eid;
	void *ctx;

	while (wcn_ce_rx_done(&ce, id, &ctx, &len) == 0) {
		b = ctx;
		(void)sys_cachectl(CACHE_INVALIDATE, b->va, b->len);
		if (len <= b->len) {
			if (id == 5) {
				/* CE5: HTT without the HTC header (Linux
				 * strips 8 bytes, unchecked). */
				if (len > HTC_HDR && htt_ep >= 0)
					data_htt(b->va + HTC_HDR,
					    len - HTC_HDR);
			} else if (htc_rx(&htc, b->va, len, &eid, &msg,
			    &mlen) == 1)
				deliver(eid, msg, mlen);
		}
		repost(id, b);
	}
	repost(id, NULL);
}

static uint8_t slot_busy[256];

static void
service_tx(unsigned id)
{
	struct rxbuf *t;
	void *ctx;

	while (wcn_ce_send_done(&ce, id, &ctx) == 0) {
		t = ctx;
		if (t != NULL && t->slot < 256)
			slot_busy[t->slot] = 0;
		free(t);
	}
}

void hw_poll(void);

static void
service_all(void)
{
	unsigned i;

	for (i = 0; i < WCN_CE_COUNT; i++) {
		if (ce.pipe[i].has_src)
			service_tx(i);
		if (ce.pipe[i].has_dst)
			service_rx(i);
	}
}

void
hw_intr(unsigned long mask)
{
	unsigned i;

	if (!started)
		return;
	for (i = 0; i < WCN_CE_COUNT; i++) {
		if (!(mask & (1ul << i)))
			continue;
		wcn_ce_intr_ack(&ce, i);
		if (ce.pipe[i].has_dst)
			service_rx(i);
		if (ce.pipe[i].has_src)
			service_tx(i);
		/* CE4, HTT's sends, has no line of its own: whenever any
		 * receive pipe is served, so is it. */
		service_tx(4);
		(void)sys_irqenable(&hooks[i]);
	}
}

void
hw_poll(void)
{

	if (started)
		service_all();
}

int
hw_take(size_t len, void **va, uint64_t *da)
{

	return own(wcn_mem_take(&mem, len, va, da));
}

/* ---- send ---- */

/*
 * One HTC message: copied into DMA memory (freed when the engine is done
 * with it), framed, cleaned, sent.  EAGAIN: no credit or no room yet.
 */
int
hw_send(unsigned eid, const void *msg, size_t len)
{
	struct rxbuf *t;
	unsigned pipe;
	int r;

	if (!started)
		return ENXIO;
	if ((t = malloc(sizeof(*t))) == NULL)
		return ENOMEM;
	/* A buffer from the arena's send area: a fixed slot per pipe. */
	t->len = HTC_HDR + len;
	if (t->len > 2048) {
		free(t);
		return EINVAL;
	}
	{
		static unsigned next;
		static uint8_t *sendarea;
		static uint64_t sendda;
		unsigned slot;

		if (sendarea == NULL) {
			void *va;

			if (wcn_mem_take(&mem, 256 * 2048, &va, &sendda) != 0) {
				free(t);
				return ENOMEM;
			}
			sendarea = va;
		}
		slot = next % 256;
		if (slot_busy[slot]) {
			/* still in flight: look at the rings, try later */
			service_all();
			if (slot_busy[slot]) {
				free(t);
				return EAGAIN;
			}
		}
		next++;
		t->slot = slot;
		t->va = sendarea + slot * 2048;
		t->da = sendda + slot * 2048;
	}
	memcpy(t->va + HTC_HDR, msg, len);
	if ((r = own(htc_frame(&htc, eid, t->va, len, &pipe))) != 0) {
		free(t);
		return r;
	}
	(void)sys_cachectl(CACHE_CLEAN, t->va, t->len);
	slot_busy[t->slot] = 1;
	if ((r = own(wcn_ce_send(&ce, pipe, t->da, t->len, eid, 0, t))) !=
	    0) {
		slot_busy[t->slot] = 0;
		free(t);
		return r;
	}
	return OK;
}

ssize_t
hw_wait(unsigned eid, void *buf, size_t size, unsigned ms)
{
	unsigned h, waited;
	size_t n;

	if (eid >= HTC_MAX_EP || rxq[eid] == NULL)
		return -EINVAL;
	for (waited = 0; ; waited++) {
		if (rxq[eid]->head != rxq[eid]->tail) {
			h = rxq[eid]->head++ % RXQ;
			n = rxq[eid]->len[h] < size ? rxq[eid]->len[h] : size;
			memcpy(buf, rxq[eid]->data[h], n);
			return (ssize_t)n;
		}
		if (waited >= ms)
			return -ETIMEDOUT;
		/* Lines come as notifications to the main loop; while a
		 * caller waits here, look at the rings ourselves. */
		service_all();
		if (rxq[eid]->head == rxq[eid]->tail)
			micro_delay(1000);
	}
}

int
hw_wmi_send(const void *msg, size_t len)
{
	unsigned t;
	int r;

	/* WMI has one credit: the previous command's comes back in a
	 * received frame's trailer. */
	for (t = 0; ; t++) {
		if ((r = hw_send((unsigned)wmi_ep, msg, len)) != EAGAIN ||
		    t >= 1000)
			return r;
		service_all();
		micro_delay(1000);
	}
}

int
hw_frame(const void *frame, size_t len, uint64_t *da)
{
	static uint8_t *slots;
	static uint64_t slots_da;
	static unsigned next;
	unsigned k;
	void *va;

	if (!started)
		return ENXIO;
	if (len > 2048)
		return EINVAL;
	if (slots == NULL) {
		if (wcn_mem_take(&mem, 16 * 2048, &va, &slots_da) != 0)
			return ENOMEM;
		slots = va;
	}
	k = next++ % 16;
	memcpy(slots + k * 2048, frame, len);
	(void)sys_cachectl(CACHE_CLEAN, slots + k * 2048, len);
	*da = slots_da + k * 2048;
	return OK;
}

/* ---- start ---- */

static int
post_all(void)
{
	struct rxbuf *b;
	unsigned i, n;
	void *va;

	for (i = 0; i < WCN_CE_COUNT; i++) {
		if (!ce.pipe[i].has_dst || ce.pipe[i].attr.rx_size == 0)
			continue;
		for (n = 0; ; n++) {
			if ((b = malloc(sizeof(*b))) == NULL)
				return ENOMEM;
			b->len = ce.pipe[i].attr.rx_size;
			if (wcn_mem_take(&mem, b->len, &va, &b->da) != 0) {
				free(b);
				return ENOMEM;
			}
			b->va = va;
			if (wcn_ce_rx_post(&ce, i, b->va, b->da, b->len, b) !=
			    0) {
				free(b);
				break;	/* full; the memory stays unused */
			}
		}
		wcn_ce_rx_commit(&ce, i);
		log_debug(&wlog, "ce%u: %u receive buffers\n", i, n);
	}
	return OK;
}

/*
 * A host table with fewer receive buffers than Linux's: the target-to-host
 * rings that carry little (the micro controller's, the packet log, the
 * second and third HTT) get 64-descriptor rings - enough, and the arena
 * stays at 8 MB.  The rings the firmware is told about (WLAN_CFG) are its
 * side; the host's sizes are ours.
 */
static struct wcn_ce_attr host_cfg[WCN_CE_COUNT];

int
hw_start(void)
{
	uint8_t msg[64], rsp[RXQ_MAX];
	unsigned i;
	size_t n;
	ssize_t got;
	int r;

	if (started)
		return EBUSY;
	if (regs == NULL && (regs = vm_map_phys(SELF,
	    (void *)(phys_bytes)dt.regs_pa, (size_t)dt.regs_sz)) == MAP_FAILED) {
		regs = NULL;
		log_warn(&wlog, "cannot map the registers\n");
		return EPERM;
	}
	if (arena == NULL) {
		if ((arena = alloc_contig(ARENA, AC_ALIGN64K, &arena_pa)) ==
		    NULL) {
			log_warn(&wlog, "no %u bytes of DMA memory\n", ARENA);
			return ENOMEM;
		}
		mem.va = arena;
		mem.da = IOVA_BASE;	/* as the engines see it */
		mem.len = ARENA;
	}
	if ((r = smmu_up()) != OK)
		return r;
	mem.used = 0;
	for (i = 0; i < HTC_MAX_EP; i++)
		if (rxq[i] == NULL && (rxq[i] = calloc(1, sizeof(*rxq[i]))) ==
		    NULL)
			return ENOMEM;

	memcpy(host_cfg, wcn_ce_host_cfg, sizeof(host_cfg));
	host_cfg[8].dst_n = host_cfg[9].dst_n = host_cfg[10].dst_n =
	    host_cfg[11].dst_n = 64;
	free(ce_ctx);
	if ((ce_ctx = calloc(wcn_ce_ctx_size(host_cfg), sizeof(void *))) ==
	    NULL)
		return ENOMEM;
	log_info(&wlog, "copy engines: rings at %x, first DMA next\n",
	    IOVA_BASE);
	if ((r = own(wcn_ce_init(&ce, &ce_ops, host_cfg, &mem, ce_ctx))) !=
	    0) {
		log_warn(&wlog, "copy engines: %d\n", r);
		return r;
	}
	if ((r = post_all()) != OK)
		return r;

	for (i = 0; i < dt.nirq && i < WCN_CE_COUNT; i++) {
		hooks[i] = (int)i;
		if ((r = sys_irqsetpolicy(dt.irq[i], 0, &hooks[i])) != OK ||
		    (r = sys_irqenable(&hooks[i])) != OK) {
			log_warn(&wlog, "line %d (ce%u): %d\n", dt.irq[i], i, r);
			return r;
		}
	}
	started = 1;
	log_info(&wlog, "copy engines up, DMA %lx+%x\n",
	    (unsigned long)arena_pa, ARENA);

	/* HTC: READY, then WMI control and HTT data, then setup complete. */
	htc_init(&htc);
	for (i = 0; i < 2000 && !htc.ready; i++) {
		service_all();
		micro_delay(1000);
	}
	if (!htc.ready) {
		log_warn(&wlog, "no HTC READY in 2 s\n");
		return ETIMEDOUT;
	}
	log_info(&wlog, "HTC ready: %u credits of %u bytes, %u endpoints, "
	    "version %d\n", htc.target_credits, htc.credit_size, htc.max_ep,
	    htc.htc_version);
	{
		static const uint16_t svc[2] = {
			HTC_SVC_WMI_CONTROL, HTC_SVC_HTT_DATA
		};

		for (i = 0; i < 2; i++) {
			htc.conn_done = 0;
			n = htc_connect_msg(msg, svc[i]);
			if ((r = hw_send(0, msg, n)) != OK)
				return r;
			for (got = 0; got < 1000 && !htc.conn_done; got++) {
				service_all();
				micro_delay(1000);
			}
			if (!htc.conn_done || htc.conn_status != 0) {
				log_warn(&wlog, "connect %04x: %s %u\n", svc[i],
				    htc.conn_done ? "status" : "no answer",
				    htc.conn_status);
				return EIO;
			}
			log_info(&wlog, "service %04x on endpoint %u\n",
			    svc[i], htc.conn_eid);
		}
	}
	wmi_ep = htc_svc_ep(&htc, HTC_SVC_WMI_CONTROL);
	htt_ep = htc_svc_ep(&htc, HTC_SVC_HTT_DATA);
	n = htc_setup_complete_msg(msg);
	if ((r = hw_send(0, msg, n)) != OK)
		return r;
	(void)rsp;
	return OK;
}

void
hw_stop(void)
{
	unsigned i;

	if (!started)
		return;
	for (i = 0; i < dt.nirq && i < WCN_CE_COUNT; i++) {
		wcn_ce_intr_enable(&ce, i, 0);
		(void)sys_irqrmpolicy(&hooks[i]);
	}
	started = 0;
}
