/*
 * The descriptor rings, and the cache maintenance that makes them mean the
 * same thing to both sides.
 *
 * This is the first real user of sys_cachectl(2).  The controller does not
 * look in the caches, so every buffer and every descriptor has to be handed
 * over explicitly: cleaned before the device reads it, invalidated before
 * and after the device writes it.  The kernel call puts a full barrier at
 * each end of the maintenance, which is also the ordering this code needs
 * before it rings the doorbell - so there is no separate barrier here, and
 * that is deliberate rather than forgotten.
 *
 * The hard part is not the buffers, which are page-aligned and a whole
 * number of cache lines long.  It is the descriptors: they are sixteen
 * bytes, a cache line is sixty-four or more, and the hardware walks them
 * packed.  So several descriptors share a line, and cleaning that line
 * writes back the driver's idea of *all* of them - including any the device
 * owns, whose writeback would be lost.
 *
 * The two directions solve it differently, and both solutions are taken
 * from drivers that run this part with caches on:
 *
 *   receive   descriptors are refilled a whole cache line at a time, so a
 *             line is either entirely the driver's or entirely the
 *             device's.  This is what U-Boot's dwc_eth_qos does, and its
 *             comment says why in as many words: "it is necessary to place
 *             multiple descriptors per cacheline in memory and do cache
 *             management carefully".
 *
 *   transmit  the driver never reads the writeback at all.  How far the
 *             device has got is in a register - the current descriptor
 *             pointer - so the transmit ring is never invalidated, and the
 *             stale ownership bits that a clean writes back behind the tail
 *             pointer are never looked at, because the tail pointer is
 *             exactly the promise that the device will not go past them.
 *
 * The alternative, which every Linux platform uses, is to put descriptors
 * in memory mapped uncached.  That is not available here: the kernel maps
 * all of RAM cacheable in its linear map and copies through it, so a second
 * mapping with different attributes would be two mappings of one page with
 * mismatched memory types, which the architecture leaves undefined.  The
 * same wall the eMMC driver hit, and the reason it moves data with the
 * processor.
 */

#include <minix/drivers.h>
#include <minix/cachectl.h>
#include <sys/mman.h>
#include <stdlib.h>

#include "dwmac.h"
#include "dwmacreg.h"

/*
 * How many descriptors share a cache line.
 *
 * The line size is not asked of the hardware - a driver cannot read
 * CTR_EL0 - so this is the largest line any ARM core in sight has, and
 * assuming too large is the safe direction: a group then spans two real
 * lines instead of one, both of them wholly inside the group, and the
 * invariant "no line holds descriptors of both owners" still holds.
 * Assuming too small would be the bug.
 */
#define DWMAC_LINE_MAX		128
#define DWMAC_DESCS_PER_GROUP	(DWMAC_LINE_MAX / (int)sizeof(struct dwmac_desc))

/*
 * The two things the whole scheme rests on, said where the compiler can
 * check them: the ring divides into whole groups, and a buffer is a whole
 * number of the largest cache line - so a buffer never shares a line with
 * anything, and a group never shares one with another group.
 */
typedef char dwmac_ring_divides[
    (DWMAC_RX_DESCS % DWMAC_DESCS_PER_GROUP == 0 &&
     DWMAC_TX_DESCS % DWMAC_DESCS_PER_GROUP == 0) ? 1 : -1];
typedef char dwmac_buf_is_lines[
    (DWMAC_BUF_SIZE % DWMAC_LINE_MAX == 0) ? 1 : -1];

/*
 * Cache maintenance, with the answer looked at.
 *
 * The first version of this driver threw the result away, and that was a
 * mistake of the kind this project keeps meeting: a call that quietly does
 * nothing looks exactly like a call that worked, and the symptom turns up
 * three layers away as memory that does not say what it should.  It is
 * reported once per kind of failure - a driver that prints on every packet
 * is a driver nobody can read.
 */
static void
cache_op(int op, void *addr, size_t len, const char *what)
{
	static int complained;
	int r;

	if ((r = sys_cachectl(op, addr, len)) != OK && !complained) {
		log_warn(&dwmac_log, "cache maintenance refused for %s: %d; "
		    "the rings will not work\n", what, r);
		complained = 1;
	}
}

static struct dwmac_desc *
desc_at(vir_bytes ring, int i)
{
	return (struct dwmac_desc *)(ring + i * sizeof(struct dwmac_desc));
}

static phys_bytes
desc_phys(phys_bytes ring, int i)
{
	return ring + i * sizeof(struct dwmac_desc);
}

/*
 * Hand a group of descriptors to the cache maintenance in one call: they
 * are contiguous, so this is one range and one pair of barriers.
 */
static void
desc_group_sync(vir_bytes ring, int first, int op)
{
	cache_op(op, desc_at(ring, first),
	    DWMAC_DESCS_PER_GROUP * sizeof(struct dwmac_desc), "descriptors");
}

/*
 * Arm one group of receive descriptors: every one of them points at its
 * buffer and belongs to the device.
 *
 * The buffers are invalidated first.  A buffer the driver has just read out
 * of has lines in the cache, and one of them written back later - by an
 * eviction, at a moment nobody chose - would land on top of what the device
 * deposited.  Invalidating before handing it over is what makes that
 * impossible; invalidating again after the packet arrives is what deals
 * with the lines a speculating core pulled in meanwhile.
 */
static void
rx_arm_group(int group)
{
	int first = group * DWMAC_DESCS_PER_GROUP;
	int i;

	for (i = first; i < first + DWMAC_DESCS_PER_GROUP; i++) {
		struct dwmac_desc *d = desc_at(dwmac.rx_ring, i);
		phys_bytes buf = dwmac.rx_buf_phys + (phys_bytes)i *
		    DWMAC_BUF_SIZE;

		cache_op(CACHE_INVALIDATE,
		    (void *)(dwmac.rx_buf + (vir_bytes)i * DWMAC_BUF_SIZE),
		    DWMAC_BUF_SIZE, "a receive buffer");

		d->des0 = (uint32_t)buf;
		d->des1 = (uint32_t)(buf >> 32);
		d->des2 = 0;
		d->des3 = RDES3_OWN | RDES3_BUF1_VALID | RDES3_IOC;
	}

	desc_group_sync(dwmac.rx_ring, first, CACHE_CLEAN);
}

/*
 * The tail pointer is the address one past the last descriptor the device
 * may use.  Everything from where it stands up to there is fair game; it
 * stops when it gets there.
 */
static void
rx_publish(int group)
{
	int last = (group + 1) * DWMAC_DESCS_PER_GROUP;

	dwmac_wr(dwmac.mac, DWMAC_DMA_CH_RXTAIL(DWMAC_DMA_CHAN),
	    (uint32_t)desc_phys(dwmac.rx_ring_phys, last % DWMAC_RX_DESCS));
}

/*
 * Does cache maintenance reach memory at all?
 *
 * A diagnostic, run once at start.  A pattern is written to a fresh buffer
 * and cleaned; the cache is then invalidated so that the next read has to
 * come from memory; what comes back says whether the clean did its job.
 * Written because the descriptor rings on the board once read back as
 * zeroes after being armed and cleaned without a word of complaint from
 * the kernel call - and a kernel primitive that quietly does nothing has to
 * be caught at the primitive, not three layers up.
 *
 * The buffer is thrown away afterwards; this is not part of running.
 */
/*
 * One shape of the question "does cache maintenance do what it says", on a
 * buffer obtained the way the caller chose: write a pattern, do the
 * operations, read back, count what changed and show what it changed into.
 */
static void
selftest_case(const char *name, void *buf, phys_bytes phys, int op1,
	size_t len1, int op2)
{
	volatile uint32_t *p = buf;
	unsigned i, bad = 0, first = 1024, last = 0;
	int r1 = 0, r2 = 0;

	for (i = 0; i < 1024; i++)
		p[i] = 0xa5000000u | i;

	if (op1 != 0)
		r1 = sys_cachectl(op1, buf, len1);
	if (op2 != 0)
		r2 = sys_cachectl(op2, buf, 4096);

	for (i = 0; i < 1024; i++) {
		if (p[i] != (0xa5000000u | i)) {
			bad++;
			if (i < first)
				first = i;
			last = i;
		}
	}

	log_info(&dwmac_log, "self-test %-14s phys %08lx r %d %d: %4u lost "
	    "(%u..%u)  p[0]=%08x p[1]=%08x p[16]=%08x p[1023]=%08x\n",
	    name, (unsigned long)phys, r1, r2, bad, bad ? first : 0,
	    bad ? last : 0, p[0], p[1], p[16], p[1023]);
}

static void
cache_selftest(void)
{
	unsigned long ctr = 0, clidr = 0;
	phys_bytes phys = 0;
	void *contig, *heap;

	if (sys_cacheinfo(&ctr, &clidr) == OK)
		log_info(&dwmac_log, "caches: CTR_EL0 %08lx (dcache line %lu), "
		    "CLIDR_EL1 %08lx (LoUU %lu LoC %lu LoUIS %lu)\n", ctr,
		    4UL << ((ctr >> 16) & 0xf), clidr,
		    (clidr >> 27) & 7, (clidr >> 24) & 7, (clidr >> 21) & 7);

	contig = alloc_contig(4096, AC_ALIGN4K, &phys);
	heap = malloc(8192);
	if (contig == NULL || heap == NULL)
		return;
	/* Line up the heap buffer the way the contiguous one is. */
	heap = (void *)(((vir_bytes)heap + 4095) & ~(vir_bytes)4095);

	/* No maintenance at all: does the memory hold what is written? */
	selftest_case("control", contig, phys, 0, 0, 0);
	/* The rings' sequence, on the rings' kind of memory. */
	selftest_case("clean+inval", contig, phys, CACHE_CLEAN, 4096,
	    CACHE_INVALIDATE);
	/* A clean alone must lose nothing: the data is still cached. */
	selftest_case("clean only", contig, phys, CACHE_CLEAN, 4096, 0);
	/* A clean of one line, to see whether the damage follows the range. */
	selftest_case("clean 64B", contig, phys, CACHE_CLEAN, 64, 0);
	/* The same on ordinary heap memory, mapped the ordinary way. */
	selftest_case("heap control", heap, 0, 0, 0, 0);
	selftest_case("heap clean", heap, 0, CACHE_CLEAN, 4096, 0);
	selftest_case("heap cl+inv", heap, 0, CACHE_CLEAN, 4096,
	    CACHE_INVALIDATE);

	free_contig(contig, 4096);
}

int
dwmac_ring_alloc(void)
{
	if (dwmac_log.log_level >= LEVEL_DEBUG)
		cache_selftest();

	size_t desc_bytes = DWMAC_RX_DESCS * sizeof(struct dwmac_desc);
	size_t tx_desc_bytes = DWMAC_TX_DESCS * sizeof(struct dwmac_desc);
	phys_bytes phys;
	void *v;

	v = alloc_contig(desc_bytes, AC_ALIGN4K, &phys);
	if (v == NULL)
		return ENOMEM;
	dwmac.rx_ring = (vir_bytes)v;
	dwmac.rx_ring_phys = phys;

	v = alloc_contig(tx_desc_bytes, AC_ALIGN4K, &phys);
	if (v == NULL)
		return ENOMEM;
	dwmac.tx_ring = (vir_bytes)v;
	dwmac.tx_ring_phys = phys;

	v = alloc_contig(DWMAC_RX_DESCS * DWMAC_BUF_SIZE, AC_ALIGN4K, &phys);
	if (v == NULL)
		return ENOMEM;
	dwmac.rx_buf = (vir_bytes)v;
	dwmac.rx_buf_phys = phys;

	v = alloc_contig(DWMAC_TX_DESCS * DWMAC_BUF_SIZE, AC_ALIGN4K, &phys);
	if (v == NULL)
		return ENOMEM;
	dwmac.tx_buf = (vir_bytes)v;
	dwmac.tx_buf_phys = phys;

	return OK;
}

void
dwmac_ring_init(void)
{
	int g;

	memset((void *)dwmac.rx_ring, 0,
	    DWMAC_RX_DESCS * sizeof(struct dwmac_desc));
	memset((void *)dwmac.tx_ring, 0,
	    DWMAC_TX_DESCS * sizeof(struct dwmac_desc));

	dwmac.rx_next = 0;
	dwmac.tx_head = 0;
	dwmac.tx_tail = 0;

	for (g = 0; g < DWMAC_RX_DESCS / DWMAC_DESCS_PER_GROUP; g++)
		rx_arm_group(g);

	/* And the transmit ring, which starts out entirely the driver's. */
	cache_op(CACHE_CLEAN, (void *)dwmac.tx_ring,
	    DWMAC_TX_DESCS * sizeof(struct dwmac_desc), "the transmit ring");
}

/*
 * One received packet, or nothing.
 *
 * A whole group is invalidated at a time rather than one descriptor,
 * because a descriptor is smaller than a cache line and there is no such
 * thing as invalidating less than one.  Doing it per group also means the
 * common case - several packets arriving together - costs one kernel call
 * for the lot.
 */
ssize_t
dwmac_ring_recv(struct netdriver_data *data, size_t max)
{
	struct dwmac_desc *d;
	size_t len;
	int group;

	group = dwmac.rx_next / DWMAC_DESCS_PER_GROUP;

	if (dwmac.rx_next % DWMAC_DESCS_PER_GROUP == 0)
		desc_group_sync(dwmac.rx_ring,
		    group * DWMAC_DESCS_PER_GROUP, CACHE_INVALIDATE);

	d = desc_at(dwmac.rx_ring, dwmac.rx_next);
	if (d->des3 & RDES3_OWN)
		return SUSPEND;			/* still the device's */

	len = 0;

	/*
	 * A packet the device flagged as bad, or one that did not fit in a
	 * single buffer, is dropped rather than passed up: this driver gives
	 * the device buffers big enough for any ethernet frame, so a packet
	 * spanning two of them is not a long packet but a confused ring.
	 */
	if ((d->des3 & RDES3_ERROR_SUMMARY) ||
	    (d->des3 & (RDES3_FIRST | RDES3_LAST)) !=
	    (RDES3_FIRST | RDES3_LAST)) {
		dwmac.rx_errors++;
	} else {
		len = d->des3 & RDES3_PACKET_LEN_MASK;
		if (len > max)
			len = max;

		if (len > 0) {
			vir_bytes buf = dwmac.rx_buf +
			    (vir_bytes)dwmac.rx_next * DWMAC_BUF_SIZE;

			/*
			 * Again, and this time for the lines the core may
			 * have pulled in while the transfer was in flight:
			 * every ARMv8 core speculates, and a stale line here
			 * is a packet with somebody else's bytes in it.
			 */
			cache_op(CACHE_INVALIDATE, (void *)buf,
			    DWMAC_BUF_SIZE, "a received buffer");

			netdriver_copyout(data, 0, (void *)buf, len);
		}
	}

	dwmac.rx_next = (dwmac.rx_next + 1) % DWMAC_RX_DESCS;

	/*
	 * The group is finished: give all of it back at once.  Doing it here
	 * rather than per descriptor is what keeps a cache line from ever
	 * holding descriptors of both owners.
	 */
	if (dwmac.rx_next % DWMAC_DESCS_PER_GROUP == 0) {
		rx_arm_group(group);
		rx_publish(group);
	}

	return (len > 0) ? (ssize_t)len : SUSPEND;
}

/*
 * How many transmit descriptors the device has finished with.
 *
 * Read from the channel's current-descriptor register rather than from the
 * descriptors themselves.  That is what lets the transmit ring never be
 * invalidated - see the note at the top of this file - and it is also
 * simply cheaper: one register read instead of a cache operation and a
 * walk.
 */
static void
tx_reclaim(void)
{
	phys_bytes cur;
	int idx;

	cur = dwmac_rd(dwmac.mac, DWMAC_DMA_CH_CUR_TXDESC(DWMAC_DMA_CHAN));
	if (cur < dwmac.tx_ring_phys)
		return;

	idx = (int)((cur - dwmac.tx_ring_phys) / sizeof(struct dwmac_desc));
	if (idx < 0 || idx >= DWMAC_TX_DESCS)
		return;

	dwmac.tx_tail = idx;
}

int
dwmac_ring_send(struct netdriver_data *data, size_t size)
{
	struct dwmac_desc *d;
	vir_bytes buf;
	phys_bytes phys;
	int next;

	if (size > DWMAC_BUF_SIZE)
		return OK;			/* dropped: too long */

	tx_reclaim();

	next = (dwmac.tx_head + 1) % DWMAC_TX_DESCS;
	if (next == dwmac.tx_tail)
		return SUSPEND;			/* the ring is full */

	buf = dwmac.tx_buf + (vir_bytes)dwmac.tx_head * DWMAC_BUF_SIZE;
	phys = dwmac.tx_buf_phys + (phys_bytes)dwmac.tx_head * DWMAC_BUF_SIZE;

	netdriver_copyin(data, 0, (void *)buf, size);

	/* The device is about to read this buffer, so it has to be in memory. */
	cache_op(CACHE_CLEAN, (void *)buf, size, "a transmit buffer");

	d = desc_at(dwmac.tx_ring, dwmac.tx_head);
	d->des0 = (uint32_t)phys;
	d->des1 = (uint32_t)(phys >> 32);
	d->des2 = (uint32_t)(size & TDES2_BUF1_LEN_MASK) | TDES2_IOC;
	d->des3 = TDES3_OWN | TDES3_FIRST | TDES3_LAST |
	    (uint32_t)(size & TDES3_PACKET_LEN_MASK);

	/*
	 * Publishing the descriptor is one call, and the barrier inside it is
	 * what orders the writes above against the doorbell below.  A
	 * descriptor seen by the device before the buffer it points at would
	 * send whatever the memory held before.
	 */
	cache_op(CACHE_CLEAN, d, sizeof(*d), "a transmit descriptor");

	dwmac.tx_head = next;

	dwmac_wr(dwmac.mac, DWMAC_DMA_CH_TXTAIL(DWMAC_DMA_CHAN),
	    (uint32_t)desc_phys(dwmac.tx_ring_phys, next));

	return OK;
}

/*
 * The rings as memory holds them right now, for the debug dump.
 *
 * Both groups are invalidated first so that what is printed is what the
 * device sees, not what this side last wrote.  That is safe: no descriptor
 * line is ever left dirty - every write is followed by a clean before
 * anything else happens - so there is nothing for the invalidate to lose.
 */
void
dwmac_ring_dump(void)
{
	struct dwmac_desc *d;
	int i;

	desc_group_sync(dwmac.rx_ring, 0, CACHE_INVALIDATE);
	log_debug(&dwmac_log, "rx desc 0-7 des3: %08x %08x %08x %08x "
	    "%08x %08x %08x %08x\n",
	    desc_at(dwmac.rx_ring, 0)->des3, desc_at(dwmac.rx_ring, 1)->des3,
	    desc_at(dwmac.rx_ring, 2)->des3, desc_at(dwmac.rx_ring, 3)->des3,
	    desc_at(dwmac.rx_ring, 4)->des3, desc_at(dwmac.rx_ring, 5)->des3,
	    desc_at(dwmac.rx_ring, 6)->des3, desc_at(dwmac.rx_ring, 7)->des3);

	/* The last transmit descriptor the driver filled, written back. */
	if (dwmac.tx_head > 0 || dwmac.tx_tail > 0) {
		i = (dwmac.tx_head + DWMAC_TX_DESCS - 1) % DWMAC_TX_DESCS;
		d = desc_at(dwmac.tx_ring, i);
		cache_op(CACHE_INVALIDATE, d, sizeof(*d), "a tx descriptor");
		log_debug(&dwmac_log, "tx desc %d: %08x %08x %08x %08x%s%s%s%s\n",
		    i, d->des0, d->des1, d->des2, d->des3,
		    (d->des3 & TDES3_OWN) ? " OWN" : "",
		    (d->des3 & TDES3_WB_ERROR_SUMMARY) ? " ERR" : "",
		    (d->des3 & TDES3_WB_NO_CARRIER) ? " NO-CARRIER" : "",
		    (d->des3 & TDES3_WB_LOSS_CARRIER) ? " LOST-CARRIER" : "");
	}
}

void
dwmac_ring_free(void)
{
	if (dwmac.rx_ring != 0)
		free_contig((void *)dwmac.rx_ring,
		    DWMAC_RX_DESCS * sizeof(struct dwmac_desc));
	if (dwmac.tx_ring != 0)
		free_contig((void *)dwmac.tx_ring,
		    DWMAC_TX_DESCS * sizeof(struct dwmac_desc));
	if (dwmac.rx_buf != 0)
		free_contig((void *)dwmac.rx_buf,
		    DWMAC_RX_DESCS * DWMAC_BUF_SIZE);
	if (dwmac.tx_buf != 0)
		free_contig((void *)dwmac.tx_buf,
		    DWMAC_TX_DESCS * DWMAC_BUF_SIZE);

	dwmac.rx_ring = dwmac.tx_ring = dwmac.rx_buf = dwmac.tx_buf = 0;
}
