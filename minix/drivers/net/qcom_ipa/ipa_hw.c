/*
 * The IPA brought up: reset, the BAM and the AP's six pipes, the SRAM laid
 * out and filled by immediate commands, and the microcontroller watched -
 * ipa2-lite's ipa_probe() in its order (port/clover/hw-modem.md, section 9;
 * the register numbers and values are that driver's, GPL - as facts).
 *
 * Every buffer the IPA reads or writes is a page of this service's memory
 * at an IOVA below 4 GiB, mapped through the IPA's SMMU bank (ipa_smmu.c):
 * the BAM's descriptor addresses are 32 bits.  The bank's memory type is
 * normal non-cacheable, the service's mapping of the same pages cacheable,
 * so every buffer is cleaned before the IPA reads it and invalidated before
 * this service reads what the IPA wrote.
 */

#include "ipa.h"

#include <minix/cachectl.h>
#include <minix/syslib.h>

#include <string.h>

#define BAM_DESC_CNT_TRSHLD	(IPA_BAM + 0x0008)
#define BAM_IRQ_EN		(IPA_BAM + 0x001c)
#define BAM_CNFG_BITS		(IPA_BAM + 0x007c)
#define BAM_IRQ_SRCS_MSK_EE0	(IPA_BAM + 0x3004)
#define BAM_P_RST(n)		(IPA_BAM + 0x13004 + 0x1000 * (n))
#define BAM_P_IRQ_EN(n)		(IPA_BAM + 0x13018 + 0x1000 * (n))
#define BAM_P_RD_OFF(n)		(IPA_BAM + 0x13800 + 0x1000 * (n))
#define BAM_P_WR_OFF(n)		(IPA_BAM + 0x13818 + 0x1000 * (n))
#define BAM_P_DESC_FIFO(n)	(IPA_BAM + 0x1381c + 0x1000 * (n))
#define BAM_P_FIFO_SIZES(n)	(IPA_BAM + 0x13820 + 0x1000 * (n))
#define BAM_SW_RST		(1u << 0)
#define BAM_EN			(1u << 1)
#define BAM_ERROR_EN		(1u << 2)
#define BAM_HRESP_ERR_EN	(1u << 1)
#define BAM_IRQ			(1u << 31)
#define P_EN			(1u << 1)
#define P_DIRECTION		(1u << 3)
#define P_SYS_MODE		(1u << 5)

#define IPA_COMP_SW_RESET	(IPA_REGS + 0x003c)
#define IPA_BCR			(IPA_REGS + 0x05b0)
#define IPA_EP_CTRL(n)		(IPA_REGS + 0x0070 + 4 * (n))
#define IPA_EP_HDR(n)		(IPA_REGS + 0x0170 + 4 * (n))
#define IPA_EP_HDR_EXT(n)	(IPA_REGS + 0x01c0 + 4 * (n))
#define IPA_EP_META_MASK(n)	(IPA_REGS + 0x0220 + 4 * (n))
#define IPA_EP_MODE(n)		(IPA_REGS + 0x02c0 + 4 * (n))
#define IPA_EP_ROUTE(n)		(IPA_REGS + 0x0370 + 4 * (n))
#define IPA_EP_HOL_BLOCK_EN(n)	(IPA_REGS + 0x03c0 + 4 * (n))
#define IPA_EP_STATUS(n)	(IPA_REGS + 0x04c0 + 4 * (n))
#define IPA_SRAM_SW_FIRST	IPA_SRAM	/* v2.5 and later */
#define IPA_IRQ_UC_1		3
#define UC_INIT_COMPLETED	1
#define UC_LOADED_MARK		0x10adedffu

/* Immediate commands (ipa-hw.h). */
#define CMD_FT_V4_INIT		3
#define CMD_FT_V6_INIT		4
#define CMD_RT_V4_INIT		7
#define CMD_RT_V6_INIT		8
#define CMD_HDR_LOCAL_INIT	9
#define CMD_HDR_SYSTEM_INIT	10
#define CMD_DMA_SHARED_MEM	19

#define DESC_IMMCMD		(1u << 8)
#define DESC_EOT		(1u << 14)
#define DESC_INT		(1u << 15)

#define IOVA_BASE		0x10000000u

struct ipa_hw hw;
int ipa_test_mode;

/*
 * DRV: the AP's own rules (ipa2-lite's ipa_rules), always this size - the
 * layout the modem is told depends on it.  Words 0-1 are the empty rule
 * the default tables point at.  The rest serve the loop test only: a
 * filter on TEST_TX (routing index 1) and one on TX (index 2), each a
 * "range16" match that takes every IPv4 packet; route 1 sends to pipe 5
 * (RX), route 2 to pipe 1 (TEST_RX), both with the system header table.
 */
#define FT4_EP0_OFF		(2 + 3 * 0)
#define FT4_EP4_OFF		(2 + 3 * 1)
#define RT4_EP0_OFF		(2 + 3 * 2)
#define RT4_EP4_OFF		(2 + 3 * 3)
#define DRV_WORDS		(RT4_EP4_OFF + 3)

static const uint32_t drv_rules[DRV_WORDS] = {
	[FT4_EP0_OFF] = (1u << 4) | (1u << 21),
	[FT4_EP0_OFF + 1] = 0xffff00,
	[FT4_EP4_OFF] = (1u << 4) | (2u << 21),
	[FT4_EP4_OFF + 1] = 0xffff00,
	[RT4_EP0_OFF] = (1u << 21) | (1u << 4) | (5u << 16),
	[RT4_EP0_OFF + 1] = 0xffff00,
	[RT4_EP4_OFF] = (1u << 21) | (1u << 4) | (1u << 16),
	[RT4_EP4_OFF + 1] = 0xffff00,
};

static uint32_t next_iova = IOVA_BASE;

/*===========================================================================*
 *				DMA memory				     *
 *===========================================================================*/
int
dbuf_alloc(struct dbuf *b, size_t len, int writable)
{
	int r;

	b->len = (len + 4095) & ~(size_t)4095;
	if ((b->va = alloc_contig(b->len, AC_ALIGN4K, &b->pa)) == NULL)
		return ENOMEM;
	memset(b->va, 0, b->len);
	b->iova = next_iova;
	if ((r = ipa_smmu_map(b->iova, b->pa, b->len, writable)) != OK) {
		free_contig(b->va, b->len);
		b->va = NULL;
		return r;
	}
	next_iova += (uint32_t)b->len + 4096;	/* a hole between buffers */
	return OK;
}

void
dbuf_clean(struct dbuf *b, size_t off, size_t len)
{
	(void)sys_cachectl(CACHE_CLEAN, (uint8_t *)b->va + off, len);
}

void
dbuf_inval(struct dbuf *b, size_t off, size_t len)
{
	(void)sys_cachectl(CACHE_INVALIDATE, (uint8_t *)b->va + off, len);
}

/*===========================================================================*
 *				reset					     *
 *===========================================================================*/
static int
reset_hw(void)
{
	uint32_t v;

	wr32(ipa.base, IPA_COMP_SW_RESET, 1);
	wr32(ipa.base, IPA_COMP_SW_RESET, 0);
	wr32(ipa.base, IPA_COMP_CFG, 0x11);
	if ((v = rd32(ipa.base, IPA_VERSION)) == 0) {
		log_warn(&ilog, "IPA_VERSION reads 0: no access\n");
		return EIO;
	}
	wr32(ipa.base, IPA_BCR, 0x1fff7f);		/* v2.5 and later */

	wr32(ipa.base, BAM_CTRL, BAM_SW_RST);
	wr32(ipa.base, BAM_CTRL, 0);
	wr32(ipa.base, BAM_DESC_CNT_TRSHLD, 0x10);
	wr32(ipa.base, BAM_CNFG_BITS, ~(1u << 11));
	wr32(ipa.base, BAM_CTRL, rd32(ipa.base, BAM_CTRL) | BAM_EN);
	wr32(ipa.base, BAM_IRQ_EN, BAM_ERROR_EN | BAM_HRESP_ERR_EN);
	wr32(ipa.base, BAM_IRQ_SRCS_MSK_EE0, BAM_IRQ);
	return OK;
}

/*===========================================================================*
 *				SRAM layout				     *
 *===========================================================================*/
/* One part after a canary word, aligned (ipa_partition_put()). */
static void
put(uint32_t *off, int id, uint32_t words, uint32_t align_words)
{
	uint32_t canary = 0xdeadbeafu, at;
	int first = 1;

	at = IPA_SRAM_SW_FIRST + *off;
	if (id == MEM_DRV) {
		hw.drv_at = at;
		if (rd32(ipa.base, at) == UC_LOADED_MARK)
			canary = UC_LOADED_MARK;
	}
	while ((first || (*off % (4 * align_words)) != 0) &&
	    *off < hw.smem_size) {
		wr32(ipa.base, at, canary);
		at += 4;
		*off += 4;
		first = 0;
	}
	hw.part[id].offset = *off + hw.smem_restr;
	hw.part[id].size = words * 4;
	*off += words * 4;
}

static int
partition(void)
{
	uint32_t v, off;

	v = rd32(ipa.base, IPA_SHARED_MEM);
	hw.smem_restr = v >> 16;
	hw.smem_size = v & 0xffff;
	if (hw.smem_restr > hw.smem_size || (hw.smem_restr & 3) ||
	    (hw.smem_size & 3) || IPA_SRAM_SW_FIRST + hw.smem_size > ipa.sz) {
		log_warn(&ilog, "SHARED_MEM %08x is not one this lays out\n",
		    v);
		return EINVAL;
	}
	hw.smem_size -= hw.smem_restr;
	off = 0x280;
	put(&off, MEM_FT_V4, IPA_NUM_PIPES + 2, 2);
	put(&off, MEM_FT_V6, IPA_NUM_PIPES + 2, 2);
	put(&off, MEM_RT_V4, 7, 2);
	put(&off, MEM_RT_V6, 7, 2);
	put(&off, MEM_MDM_HDR, 80, 2);
	put(&off, MEM_DRV, DRV_WORDS, 1);
	put(&off, MEM_MDM_COMP, 128, 2);	/* v2.6 */
	put(&off, MEM_MDM, (hw.smem_size - off) / 4 - 2, 1);
	put(&off, MEM_END, 0, 2);
	return OK;
}

/*===========================================================================*
 *				pipes					     *
 *===========================================================================*/
static int
is_rx(unsigned ep)
{
	return ep == EP_TEST_RX || ep == EP_LAN_RX || ep == EP_RX;
}

static void
pipe_reset(unsigned ep)
{
	struct pipe *p = &hw.pipe[ep];

	wr32(ipa.base, IPA_EP_CTRL(ep), ep != EP_CMD);	/* delayed */
	wr32(ipa.base, IPA_EP_HOL_BLOCK_EN(ep), is_rx(ep));
	wr32(ipa.base, BAM_P_CTRL(ep), 0);
	wr32(ipa.base, BAM_P_RST(ep), 1);
	wr32(ipa.base, BAM_P_RST(ep), 0);
	p->head = p->tail = 0;
	wr32(ipa.base, BAM_P_RD_OFF(ep), 0);
	wr32(ipa.base, BAM_P_WR_OFF(ep), 0);
	wr32(ipa.base, BAM_P_DESC_FIFO(ep), p->fifo.iova);
	wr32(ipa.base, BAM_P_FIFO_SIZES(ep), FIFO_BYTES);
	wr32(ipa.base, BAM_P_IRQ_EN(ep), 0);
	wr32(ipa.base, BAM_P_CTRL(ep), P_SYS_MODE | P_EN |
	    (is_rx(ep) ? P_DIRECTION : 0));
}

/* The endpoint's own registers (ipa_setup_ep()), after its pipe's reset. */
static void
pipe_setup_regs(unsigned ep)
{
	wr32(ipa.base, BAM_IRQ_SRCS_MSK_EE0,
	    rd32(ipa.base, BAM_IRQ_SRCS_MSK_EE0) | (1u << ep));

	switch (ep) {
	case EP_LAN_RX:
		wr32(ipa.base, IPA_EP_HDR(ep), 0x00000002);
		wr32(ipa.base, IPA_EP_HDR_EXT(ep), 0x00000803);
		wr32(ipa.base, IPA_EP_META_MASK(ep), 0);
		wr32(ipa.base, IPA_EP_STATUS(ep), 0x00000001);
		break;
	case EP_TX:
	case EP_TEST_TX:
		wr32(ipa.base, IPA_EP_HDR(ep), ipa_test_mode ? 0xc4 : 0x44);
		wr32(ipa.base, IPA_EP_HDR_EXT(ep), 0x00000001);
		wr32(ipa.base, IPA_EP_STATUS(ep), 0x00000005);
		wr32(ipa.base, IPA_EP_ROUTE(ep), 0x00000007);
		wr32(ipa.base, IPA_EP_MODE(ep), 0x00000020);
		break;
	case EP_RX:
	case EP_TEST_RX:
		wr32(ipa.base, IPA_EP_HDR(ep), 0x002800c4);
		wr32(ipa.base, IPA_EP_HDR_EXT(ep), 0x0000000b);
		wr32(ipa.base, IPA_EP_META_MASK(ep), 0xff000000);
		break;
	}
}

/* The pipe's read offset, as a descriptor index. */
static unsigned
rd_index(unsigned ep)
{
	return (rd32(ipa.base, BAM_P_RD_OFF(ep)) & 0xffff) / DESC_BYTES &
	    (FIFO_NDESC - 1);
}

/* Descriptors onto a pipe: written, cleaned, then the write offset. */
int
pipe_put(unsigned ep, const struct bam_desc *d, unsigned n)
{
	struct pipe *p = &hw.pipe[ep];
	unsigned i, t;

	for (i = 0, t = p->tail; i < n; i++) {
		if (((t + 1) & (FIFO_NDESC - 1)) == p->head)
			return EBUSY;
		p->desc[t] = d[i];
		dbuf_clean(&p->fifo, t * DESC_BYTES, DESC_BYTES);
		t = (t + 1) & (FIFO_NDESC - 1);
	}
	p->tail = t;
	__asm__ volatile("dsb st" ::: "memory");
	wr32(ipa.base, BAM_P_WR_OFF(ep), t * DESC_BYTES);
	return OK;
}

/* Wait until the pipe has consumed everything put on it. */
static int
pipe_drain(unsigned ep, unsigned ms)
{
	struct pipe *p = &hw.pipe[ep];
	unsigned t;

	for (t = 0; t < ms * 10; t++) {
		if (rd_index(ep) == p->tail) {
			p->head = p->tail;
			return OK;
		}
		micro_delay(100);
	}
	log_warn(&ilog, "pipe %u: read offset %u, %u put; the IPA did not "
	    "take them\n", ep, rd_index(ep), p->tail);
	return ETIMEDOUT;
}

/*===========================================================================*
 *				SRAM, by immediate commands		     *
 *===========================================================================*/
static int
sram_part(int id)
{
	struct ipa_part *part = &hw.part[id];
	struct dbuf pld, args;
	struct bam_desc d[3];
	uint32_t *w, val, n, i;
	uint64_t *a;
	unsigned nd = 0;
	int r;

	if (part->size == 0)
		return OK;
	if ((r = dbuf_alloc(&args, 3 * 16, 0)) != OK ||
	    (r = dbuf_alloc(&pld, part->size + 8, 0)) != OK)
		return r;
	w = pld.va;
	n = part->size / 4;
	switch (id) {
	case MEM_FT_V4:
	case MEM_FT_V6:
		*w++ = 0x1fffff;
		n--;
		/* FALLTHROUGH */
	case MEM_RT_V4:
	case MEM_RT_V6:
		/* Every rule to the (empty) rules in DRV. */
		val = hw.part[MEM_DRV].offset - part->offset;
		for (i = 0; i <= n; i++)
			w[i] = val | 1;
		break;
	case MEM_DRV:
		memcpy(w, drv_rules, sizeof(drv_rules));
		break;
	default:
		break;
	}
	if (ipa_test_mode && (id == MEM_FT_V4 || id == MEM_RT_V4)) {
		/* The loop's filters and routes, in the v4 tables. */
		w = pld.va;
		val = hw.part[MEM_DRV].offset - part->offset + 1;
		if (id == MEM_FT_V4) {
			w[2 + 0] = val + FT4_EP0_OFF * 4;
			w[2 + 4] = val + FT4_EP4_OFF * 4;
		} else {
			w[1] = val + RT4_EP0_OFF * 4;
			w[2] = val + RT4_EP4_OFF * 4;
		}
	}
	dbuf_clean(&pld, 0, pld.len);

	a = args.va;
	switch (id) {
	case MEM_MDM_HDR:
		if (hw.sys_hdr.va == NULL &&
		    (r = dbuf_alloc(&hw.sys_hdr, 2048, 1)) != OK)
			return r;
		a[0] = hw.sys_hdr.iova;			/* HDR_SYSTEM_INIT */
		a[1] = 0;
		d[nd].addr = args.iova;
		d[nd].size = CMD_HDR_SYSTEM_INIT;
		d[nd++].flags = DESC_IMMCMD | DESC_EOT;
		a[2] = (uint64_t)pld.iova | ((uint64_t)(part->size & 0xfff) <<
		    32) | ((uint64_t)(part->offset & 0xffff) << 44);
		a[3] = 0;				/* HDR_LOCAL_INIT */
		d[nd].addr = args.iova + 16;
		d[nd].size = CMD_HDR_LOCAL_INIT;
		d[nd++].flags = DESC_IMMCMD | DESC_EOT;
		/* FALLTHROUGH */
	case MEM_MDM_COMP:
	case MEM_MDM:
	case MEM_DRV:
		/* DMA_SHARED_MEM: size in the top half of word 0, the
		 * system address, the local one (to the SRAM). */
		w = (uint32_t *)(void *)((uint8_t *)args.va + 16 * nd);
		w[0] = (part->size & 0xffff) << 16;
		w[1] = pld.iova;
		w[2] = part->offset & 0xffff;
		w[3] = 0;
		d[nd].addr = args.iova + 16 * nd;
		d[nd].size = CMD_DMA_SHARED_MEM;
		d[nd++].flags = DESC_IMMCMD | DESC_EOT;
		break;
	case MEM_RT_V4:
	case MEM_FT_V4:
		a[0] = (uint64_t)pld.iova | ((uint64_t)(part->size & 0xfff) <<
		    32) | ((uint64_t)(part->offset & 0xffff) << 44);
		a[1] = 0;
		d[nd].addr = args.iova;
		d[nd].size = id == MEM_RT_V4 ? CMD_RT_V4_INIT : CMD_FT_V4_INIT;
		d[nd++].flags = DESC_IMMCMD | DESC_EOT;
		break;
	case MEM_RT_V6:
	case MEM_FT_V6:
		a[0] = (uint64_t)pld.iova | ((uint64_t)(part->size & 0xffff) <<
		    32) | ((uint64_t)(part->offset & 0xffff) << 48);
		a[1] = 0;
		d[nd].addr = args.iova;
		d[nd].size = id == MEM_RT_V6 ? CMD_RT_V6_INIT : CMD_FT_V6_INIT;
		d[nd++].flags = DESC_IMMCMD | DESC_EOT;
		break;
	}
	dbuf_clean(&args, 0, args.len);
	if ((r = pipe_put(EP_CMD, d, nd)) != OK)
		return r;
	return pipe_drain(EP_CMD, 100);
	/* pld and args stay mapped: a handful of pages, once. */
}

static int
sram_init(void)
{
	static const int order[] = { MEM_DRV, MEM_FT_V4, MEM_FT_V6,
	    MEM_RT_V4, MEM_RT_V6, MEM_MDM_HDR, MEM_MDM_COMP, MEM_MDM };
	unsigned i;
	int r;

	for (i = 0; i < sizeof(order) / sizeof(order[0]); i++) {
		if ((r = sram_part(order[i])) != OK) {
			log_warn(&ilog, "SRAM part %d (%x+%x): %d\n", order[i],
			    hw.part[order[i]].offset, hw.part[order[i]].size,
			    r);
			return r;
		}
	}
	return OK;
}

/*===========================================================================*
 *				ipa_hw_init				     *
 *===========================================================================*/
int
ipa_hw_init(void)
{
	unsigned ep;
	int r;

	if ((r = ipa_rpm_clock(40000)) != OK) {
		log_warn(&ilog, "the IPA's clock: RPM vote failed (%d)\n", r);
		return r;
	}
	if ((r = ipa_smmu_init()) != OK)
		return r;
	for (ep = 0; ep < EP_NUM; ep++)
		if ((r = dbuf_alloc(&hw.pipe[ep].fifo, FIFO_BYTES,
		    is_rx(ep))) != OK)
			return r;
	if ((r = ipa_smmu_route()) != OK)
		return r;

	log_info(&ilog, "resetting the IPA and its BAM\n");
	if ((r = reset_hw()) != OK || (r = partition()) != OK)
		return r;
	for (ep = 0; ep < EP_NUM; ep++) {
		hw.pipe[ep].desc = hw.pipe[ep].fifo.va;
		dbuf_clean(&hw.pipe[ep].fifo, 0, hw.pipe[ep].fifo.len);
		pipe_reset(ep);
		pipe_setup_regs(ep);
	}
	wr32(ipa.base, IPA_ROUTE, 0x00040044);

	log_info(&ilog, "SRAM %u bytes (restricted %u): DRV %x, MDM %x+%x; "
	    "filling it through the command pipe\n", hw.smem_size,
	    hw.smem_restr, hw.part[MEM_DRV].offset, hw.part[MEM_MDM].offset,
	    hw.part[MEM_MDM].size);
	r = sram_init();
	ipa_smmu_report();
	if (r != OK)
		return r;
	log_info(&ilog, "IPA version %08x (hw %08x), BAM revision %08x, %u "
	    "bytes SRAM: immediate commands went through the BAM\n",
	    rd32(ipa.base, IPA_VERSION), rd32(ipa.base, IPA_COMP_HW_VERSION),
	    rd32(ipa.base, BAM_REVISION), hw.smem_size);

	/* The microcontroller's word, once the modem has loaded it. */
	wr32(ipa.base, IPA_IRQ_EN_EE0, rd32(ipa.base, IPA_IRQ_EN_EE0) |
	    (1u << IPA_IRQ_UC_1));
	hw.uc_loaded = rd32(ipa.base, hw.drv_at) == UC_LOADED_MARK;
	return OK;
}

/* The microcontroller said it is up (polled; its line is the IPA's). */
int
ipa_uc_poll(void)
{
	uint32_t st;

	if (hw.uc_loaded)
		return 1;
	st = rd32(ipa.base, IPA_IRQ_STTS_EE0);
	if (st == 0)
		return 0;
	wr32(ipa.base, IPA_IRQ_CLR_EE0, st);
	if ((st & (1u << IPA_IRQ_UC_1)) &&
	    (rd32(ipa.base, IPA_UC_RESP) & 0xff) == UC_INIT_COMPLETED) {
		hw.uc_loaded = 1;
		wr32(ipa.base, hw.drv_at, UC_LOADED_MARK);
		log_info(&ilog, "microcontroller initialized\n");
	}
	return hw.uc_loaded;
}

/*===========================================================================*
 *				the loop test				     *
 *===========================================================================*/
/*
 * With ipa_test_mode, an IPv4/UDP packet behind a 4-byte QMAP header on
 * TEST_TX must come out of RX, and one on TX out of TEST_RX - the IPA's
 * filter and routing blocks, both BAM directions and the SMMU's
 * translation for every buffer, without the modem (ipa2-lite's test_mode).
 */
static unsigned
make_packet(uint8_t *b, unsigned seq)
{
	static const char text[] = "MEECHO IPA loop";
	unsigned ip_len = 20 + 8 + sizeof(text), i, sum = 0;
	uint8_t *ip = b + 4;

	memset(b, 0, 4 + ip_len);
	b[1] = 1;				/* QMAP mux id */
	b[2] = ip_len >> 8;
	b[3] = ip_len & 0xff;
	ip[0] = 0x45;
	ip[2] = ip_len >> 8;
	ip[3] = ip_len & 0xff;
	ip[4] = seq >> 8;
	ip[5] = seq & 0xff;
	ip[8] = 64;
	ip[9] = 17;				/* UDP */
	ip[12] = 10; ip[15] = 1;		/* 10.0.0.1 */
	ip[16] = 10; ip[19] = 2;		/* 10.0.0.2 */
	for (i = 0; i < 20; i += 2)
		sum += ip[i] << 8 | ip[i + 1];
	while (sum >> 16)
		sum = (sum & 0xffff) + (sum >> 16);
	sum = ~sum & 0xffff;
	ip[10] = sum >> 8;
	ip[11] = sum & 0xff;
	ip[20] = 0x30; ip[21] = 0x39;		/* port 12345 */
	ip[22] = 0x30; ip[23] = 0x39;
	ip[24] = (8 + sizeof(text)) >> 8;
	ip[25] = (8 + sizeof(text)) & 0xff;
	memcpy(ip + 28, text, sizeof(text));
	ip[28 + sizeof(text) - 1] = (uint8_t)('0' + seq % 10);
	return 4 + ip_len;
}

static int
loop_one(unsigned tx_ep, unsigned rx_ep, unsigned seq)
{
	struct pipe *rp = &hw.pipe[rx_ep];
	struct dbuf txb, rxb;
	struct bam_desc d;
	volatile struct bam_desc *got;
	unsigned len, t, gotlen, slot;
	uint8_t *out, *in;
	int r;

	if ((r = dbuf_alloc(&txb, 2048, 0)) != OK ||
	    (r = dbuf_alloc(&rxb, 2048, 1)) != OK)
		return r;
	out = txb.va;
	len = make_packet(out, seq);
	dbuf_clean(&txb, 0, txb.len);
	memset(rxb.va, 0xa5, rxb.len);
	dbuf_clean(&rxb, 0, rxb.len);
	dbuf_inval(&rxb, 0, rxb.len);

	/* A buffer to receive into, then the packet. */
	slot = rp->tail;
	d.addr = rxb.iova;
	d.size = 2048;
	d.flags = DESC_INT;
	if ((r = pipe_put(rx_ep, &d, 1)) != OK)
		return r;
	wr32(ipa.base, IPA_EP_HOL_BLOCK_EN(rx_ep), 0);
	wr32(ipa.base, IPA_EP_CTRL(rx_ep), 0);
	wr32(ipa.base, IPA_EP_CTRL(tx_ep), 0);
	d.addr = txb.iova;
	d.size = (uint16_t)len;
	d.flags = DESC_EOT;
	if ((r = pipe_put(tx_ep, &d, 1)) != OK)
		return r;

	for (t = 0; t < 1000; t++) {
		if (rd_index(rx_ep) != slot)
			break;
		micro_delay(1000);
	}
	log_info(&ilog, "loop %u->%u: tx read offset %u (of %u), rx %u (of "
	    "%u) after %u ms\n", tx_ep, rx_ep, rd_index(tx_ep),
	    hw.pipe[tx_ep].tail, rd_index(rx_ep), rp->tail, t);
	if (rd_index(rx_ep) == slot) {
		ipa_smmu_report();
		log_warn(&ilog, "loop %u->%u: nothing came out; IRQ_STTS %08x\n",
		    tx_ep, rx_ep, rd32(ipa.base, IPA_IRQ_STTS_EE0));
		return ETIMEDOUT;
	}
	dbuf_inval(&rp->fifo, slot * DESC_BYTES, DESC_BYTES);
	got = &rp->desc[slot];
	gotlen = got->size;
	dbuf_inval(&rxb, 0, rxb.len);
	in = rxb.va;
	log_info(&ilog, "loop %u->%u: %u bytes in (sent %u): %02x %02x %02x "
	    "%02x | %02x %02x .. %02x %02x\n", tx_ep, rx_ep, gotlen, len,
	    in[0], in[1], in[2], in[3], in[4], in[5], in[gotlen > 2 ?
	    gotlen - 2 : 0], in[gotlen > 1 ? gotlen - 1 : 0]);
	rp->head = rp->tail;
	hw.pipe[tx_ep].head = hw.pipe[tx_ep].tail;
	if (gotlen < len || memcmp(in + 4, out + 4, len - 4) != 0) {
		log_warn(&ilog, "loop %u->%u: the IP packet differs\n", tx_ep,
		    rx_ep);
		return EIO;
	}
	log_info(&ilog, "loop %u->%u: the IP packet came back whole\n",
	    tx_ep, rx_ep);
	return OK;
}

int
ipa_loop_test(void)
{
	int r1, r2;

	r1 = loop_one(EP_TEST_TX, EP_RX, 1);
	r2 = loop_one(EP_TX, EP_TEST_RX, 2);
	log_info(&ilog, "loop test: %s\n", r1 == OK && r2 == OK ? "both "
	    "ways work" : "FAILED");
	return r1 != OK ? r1 : r2;
}
