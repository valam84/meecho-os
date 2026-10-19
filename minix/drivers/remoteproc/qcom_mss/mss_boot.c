/*
 * Starting the modem: Linux's qcom_q6v5_mss.c for MSS_SDM660, in its
 * order (q6v5_start(): q6v5_mba_load(), q6v5_mpss_load(), the wait for
 * "ready") - registers, values and order from there, not its code.
 * port/clover/hw-modem.md, section 4.
 *
 *   1. Proxy votes: CX and MX at their highest level, enabled (rpmpd for
 *      "cx" and "mx" at INT_MAX - qcom_rpm aggregates them with the
 *      GPU's); the GCC branches of the node; MSS_RESTART let go.
 *   2. The image region and the MBA's region to the modem (TrustZone's
 *      MP/ASSIGN: HLOS -> MSS_MSA), the MBA's address in RMB, the Hexagon
 *      powered and started (q6v5proc_reset(), the QDSP6v62 branch), the
 *      PBL's status, then the MBA's: XPU unlocked.
 *   3. The metadata (ELF headers and hashes) in a buffer of our RAM given
 *      to the modem, its address in RMB, META_DATA_READY; the MBA says
 *      META_DATA_AUTH_SUCCESS; the buffer back.
 *   4. The image region back to us and shared, every segment copied in,
 *      the running length in RMB after each (the first with LOAD_READY);
 *      the region to the modem alone; the MBA says AUTH_COMPLETE.
 *   5. The modem's own smp2p entry says "ready"; the MBA's region back.
 *      On "handover" the proxy votes are let go (qcom_msa_handover()).
 *
 * Nothing waits on an interrupt: the RMB and smp2p are polled, as Linux
 * itself polls the RMB.
 */

#include "mss.h"
#include "adreno_mdt.h"
#include "qcom_soc.h"

#include <minix/qcomscm.h>
#include <minix/qcom_rpm.h>
#include <minix/ds.h>
#include <minix/vm.h>
#include <sys/mman.h>

#include <stdlib.h>
#include <string.h>

/* RMB (the PBL's and MBA's mailbox). */
#define RMB_MBA_IMAGE		0x00
#define RMB_PBL_STATUS		0x04
#define RMB_MBA_COMMAND		0x08
#define RMB_MBA_STATUS		0x0c
#define RMB_PMI_META_DATA	0x10
#define RMB_PMI_CODE_START	0x14
#define RMB_PMI_CODE_LENGTH	0x18

#define RMB_PBL_SUCCESS			1
#define RMB_MBA_XPU_UNLOCKED		1
#define RMB_MBA_XPU_UNLOCKED_SCRIBBLED	2
#define RMB_MBA_META_DATA_AUTH_SUCCESS	3
#define RMB_MBA_AUTH_COMPLETE		4
#define RMB_CMD_META_DATA_READY		1
#define RMB_CMD_LOAD_READY		2

/* QDSP6SS. */
#define QDSP6SS_RESET		0x014
#define QDSP6SS_GFMUX_CTL	0x020
#define QDSP6SS_PWR_CTL		0x030
#define QDSP6V6SS_MEM_PWR_CTL	0x034
#define QDSP6SS_XO_CBCR		0x038
#define QDSP6V62SS_BHS_STATUS	0x0c4
#define QDSP6SS_STRAP_ACC	0x110

#define Q6SS_STOP_CORE		(1u << 0)
#define Q6SS_CORE_ARES		(1u << 1)
#define Q6SS_BUS_ARES_ENABLE	(1u << 2)
#define Q6SS_CBCR_CLKEN		(1u << 0)
#define Q6SS_CBCR_CLKOFF	(1u << 31)
#define Q6SS_CLK_ENABLE		(1u << 1)
#define Q6SS_L2DATA_STBY_N	(1u << 18)
#define Q6SS_SLP_RET_N		(1u << 19)
#define Q6SS_CLAMP_IO		(1u << 20)
#define QDSP6V56_CLAMP_WL	(1u << 21)
#define QDSP6V56_CLAMP_QMC_MEM	(1u << 22)
#define QDSP6V56_BHS_ON		(1u << 24)
#define QDSP6V56_LDO_BYP	(1u << 25)
#define QDSP6V55_BHS_EN_REST_ACK (1u << 0)
#define QDSP6SS_ACC_OVERRIDE_VAL 0x20

/* AXI halt ports, in the TCSR. */
#define AXI_HALTREQ		0x0
#define AXI_HALTACK		0x4
#define AXI_IDLE		0x8

/* The modem's smp2p bits ("slave-kernel"), and ours ("master-kernel"). */
#define SMP2P_FATAL		(1u << 0)
#define SMP2P_READY		(1u << 1)
#define SMP2P_HANDOVER		(1u << 2)
#define SMP2P_STOP_ACK		(1u << 3)

#define LEVEL_TURBO		384	/* RPM_SMD_LEVEL_TURBO, sdm660 max */
#define META_MAX		(64 * 1024)
#define SEG_MAX			(64 * 1024 * 1024)

#define HLOS			(1u << QCOMSCM_VMID_HLOS)
#define MSS_MSA			(1u << QCOMSCM_VMID_MSS_MSA)

static int proxy_held;

static uint32_t
rd(volatile uint8_t *b, unsigned off)
{
	return *(volatile uint32_t *)(b + off);
}

static void
wr(volatile uint8_t *b, unsigned off, uint32_t v)
{
	*(volatile uint32_t *)(b + off) = v;
}

static void
msleep(unsigned ms)
{
	/* A tick is 10 ms; below that, spin. */
	if (ms >= 10)
		(void)tickdelay((ms + 9) / 10);
	else
		micro_delay(ms * 1000);
}

/*===========================================================================*
 *				rpm					     *
 *===========================================================================*/
static int
rpm_vote(uint32_t res, uint32_t key, uint32_t value)
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
	m.m_u32.data[QRPM_TYPE] = res;
	m.m_u32.data[QRPM_ID] = 0;
	m.m_u32.data[QRPM_KEY] = key;
	m.m_u32.data[QRPM_VALUE] = value;
	if ((r = ipc_sendrec(ep, &m)) != OK) {
		ep = NONE;
		return r;
	}
	return m.m_type;
}

/* The proxy votes on (rpmpd: the level, then on) or off. */
static int
proxy_votes(int on)
{
	static const uint32_t rails[2] = { QRPM_RES_RWCX, QRPM_RES_RWMX };
	int i, r, bad = 0;

	for (i = 0; i < 2; i++) {
		if ((r = rpm_vote(rails[i], QRPM_KEY_LEVEL, on ? LEVEL_TURBO :
		    0)) != OK ||
		    (r = rpm_vote(rails[i], QRPM_KEY_ENABLE, on ? 1 : 0)) != OK) {
			log_warn(&mlog, "%s %s: qcom_rpm says %d\n", i ? "mx" :
			    "cx", on ? "up" : "down", r);
			bad = r;
		}
	}
	return bad;
}

/*===========================================================================*
 *				xfer					     *
 *===========================================================================*/
/* q6v5_xfer_mem_ownership(): to us (local), to the modem (remote), both. */
static int
xfer(uint32_t *perm, int local, int remote, volatile uint8_t *va, size_t len,
	const char *what)
{
	struct qcomscm_answer a;
	uint32_t dst = 0, next = 0;
	int r, n = 0;

	if (local == !!(*perm & HLOS) && remote == !!(*perm & MSS_MSA))
		return OK;
	if (local) {
		dst |= QCOMSCM_DST(QCOMSCM_VMID_HLOS, QCOMSCM_PERM_RWX) << (8 * n++);
		next |= HLOS;
	}
	if (remote) {
		dst |= QCOMSCM_DST(QCOMSCM_VMID_MSS_MSA, QCOMSCM_PERM_RW) <<
		    (8 * n++);
		next |= MSS_MSA;
	}
	len = (len + 4095) & ~(size_t)4095;
	r = sys_qcomscm_assign((void *)(uintptr_t)va, len, *perm, dst, &a);
	if (r != OK || a.status != 0 || a.res[0] != 0) {
		log_warn(&mlog, "assign %s (%zu bytes) %x -> %x: call %d, err "
		    "%ld, %lx\n", what, len, *perm, next, r, (long)a.status,
		    (unsigned long)a.res[0]);
		return r != OK ? r : EIO;
	}
	log_debug(&mlog, "assign %s: %x -> %x\n", what, *perm, next);
	*perm = next;
	return OK;
}

/*===========================================================================*
 *				halt_axi				     *
 *===========================================================================*/
static void
halt_axi(unsigned off, const char *name)
{
	unsigned t;

	if (rd(mss.halt, off + AXI_IDLE))
		return;
	wr(mss.halt, off + AXI_HALTREQ, 1);
	for (t = 0; t < 100 && !rd(mss.halt, off + AXI_HALTACK); t++)
		msleep(1);
	if (!rd(mss.halt, off + AXI_IDLE))
		log_warn(&mlog, "AXI port %s did not halt\n", name);
	wr(mss.halt, off + AXI_HALTREQ, 0);
}

static void
halt_all(void)
{
	halt_axi(mss.halt_q6, "q6");
	halt_axi(mss.halt_modem, "modem");
	halt_axi(mss.halt_nc, "nc");
}

/*===========================================================================*
 *				clocks					     *
 *===========================================================================*/
static int
clocks_on(void)
{
	unsigned i;
	int r, bad = 0;

	for (i = 0; i < mss.nclk; i++) {
		if (mss.clk[i].ph != qsoc_gcc_phandle()) {
			log_debug(&mlog, "clock %s: not the GCC's; the RPM keeps "
			    "it on\n", mss.clk[i].name);
			continue;
		}
		r = qsoc_clk_enable(mss.clk[i].id, 0);
		if (r == ENOENT)
			log_info(&mlog, "clock %s (%u): not one the GCC "
			    "registers (Linux gets none either)\n",
			    mss.clk[i].name, mss.clk[i].id);
		else if (r != OK) {
			log_warn(&mlog, "clock %s (%u): %d\n", mss.clk[i].name,
			    mss.clk[i].id, r);
			bad = r;
		}
	}
	return bad;
}

/*===========================================================================*
 *				q6_start				     *
 *===========================================================================*/
/* q6v5proc_reset(), the MSM8998/SDM660 branch; then the PBL's status. */
static int
q6_start(void)
{
	volatile uint8_t *q = mss.qdsp6;
	uint32_t val;
	unsigned t;
	int i;

	wr(q, QDSP6SS_STRAP_ACC, QDSP6SS_ACC_OVERRIDE_VAL);

	val = rd(q, QDSP6SS_RESET);
	val |= Q6SS_CORE_ARES | Q6SS_BUS_ARES_ENABLE | Q6SS_STOP_CORE;
	wr(q, QDSP6SS_RESET, val);

	val = rd(q, QDSP6SS_XO_CBCR) | Q6SS_CBCR_CLKEN;
	wr(q, QDSP6SS_XO_CBCR, val);
	for (t = 0; t < 200 && (rd(q, QDSP6SS_XO_CBCR) & Q6SS_CBCR_CLKOFF);
	    t++)
		micro_delay(1);
	if (rd(q, QDSP6SS_XO_CBCR) & Q6SS_CBCR_CLKOFF) {
		log_warn(&mlog, "Q6 XO clock did not start\n");
		return ETIMEDOUT;
	}

	val = rd(q, QDSP6SS_PWR_CTL) | QDSP6V56_BHS_ON;
	wr(q, QDSP6SS_PWR_CTL, val);
	val |= rd(q, QDSP6SS_PWR_CTL);
	micro_delay(1);
	for (t = 0; t < 200 && !(rd(q, QDSP6V62SS_BHS_STATUS) &
	    QDSP6V55_BHS_EN_REST_ACK); t++)
		micro_delay(1);
	if (!(rd(q, QDSP6V62SS_BHS_STATUS) & QDSP6V55_BHS_EN_REST_ACK)) {
		log_warn(&mlog, "BHS_EN_REST_ACK not set\n");
		return ETIMEDOUT;
	}

	val |= QDSP6V56_LDO_BYP;
	wr(q, QDSP6SS_PWR_CTL, val);

	val = rd(q, QDSP6SS_PWR_CTL) & ~QDSP6V56_CLAMP_QMC_MEM;
	wr(q, QDSP6SS_PWR_CTL, val);
	val |= Q6SS_L2DATA_STBY_N | Q6SS_SLP_RET_N;
	wr(q, QDSP6SS_PWR_CTL, val);

	/* L1, L2, ETB and JU memories, one at a time. */
	val = rd(q, QDSP6V6SS_MEM_PWR_CTL);
	for (i = 28; i >= 0; i--) {
		val |= 1u << i;
		wr(q, QDSP6V6SS_MEM_PWR_CTL, val);
		val |= rd(q, QDSP6V6SS_MEM_PWR_CTL);
		micro_delay(1);
	}

	val = rd(q, QDSP6SS_PWR_CTL) & ~QDSP6V56_CLAMP_WL;
	wr(q, QDSP6SS_PWR_CTL, val);
	val &= ~Q6SS_CLAMP_IO;
	wr(q, QDSP6SS_PWR_CTL, val);

	val = rd(q, QDSP6SS_RESET) & ~Q6SS_CORE_ARES;
	wr(q, QDSP6SS_RESET, val);
	val = rd(q, QDSP6SS_GFMUX_CTL) | Q6SS_CLK_ENABLE;
	wr(q, QDSP6SS_GFMUX_CTL, val);
	val = rd(q, QDSP6SS_RESET) & ~Q6SS_STOP_CORE;
	wr(q, QDSP6SS_RESET, val);

	for (t = 0; t < 100 && rd(mss.rmb, RMB_PBL_STATUS) == 0; t++)
		msleep(10);
	val = rd(mss.rmb, RMB_PBL_STATUS);
	if (val != RMB_PBL_SUCCESS) {
		log_warn(&mlog, "PBL: %s (status %d)\n", val == 0 ?
		    "no answer in 1 s" : "unexpected", (int)val);
		return val == 0 ? ETIMEDOUT : EIO;
	}
	return OK;
}

/* The MBA's status: until it is "want" (or anything, want 0) or negative. */
static int
mba_wait(uint32_t want, unsigned ms, int32_t *got)
{
	int32_t v = 0;
	unsigned t;

	for (t = 0; t <= ms; t += 10) {
		v = (int32_t)rd(mss.rmb, RMB_MBA_STATUS);
		if (v < 0 || (want == 0 && v != 0) ||
		    (want != 0 && (uint32_t)v == want))
			break;
		msleep(10);
	}
	*got = v;
	if (v < 0)
		return EIO;
	if ((want == 0 && v == 0) || (want != 0 && (uint32_t)v != want))
		return ETIMEDOUT;
	return OK;
}

/*===========================================================================*
 *				dev_put					     *
 *===========================================================================*/
/* Bytes into memory mapped as a device: aligned words, bytes at the ends. */
static void
dev_put(volatile uint8_t *dst, const uint8_t *src, size_t n)
{
	size_t i = 0;
	uint64_t w;

	while (i < n && ((uintptr_t)(dst + i) & 7) != 0) {
		dst[i] = src != NULL ? src[i] : 0;
		i++;
	}
	for (; i + 8 <= n; i += 8) {
		w = 0;
		if (src != NULL)
			memcpy(&w, src + i, 8);
		*(volatile uint64_t *)(volatile void *)(dst + i) = w;
	}
	for (; i < n; i++)
		dst[i] = src != NULL ? src[i] : 0;
}

/*===========================================================================*
 *				map					     *
 *===========================================================================*/
static volatile uint8_t *
map(u64_t pa, u64_t sz, const char *what)
{
	void *v;

	v = vm_map_phys(SELF, (void *)(phys_bytes)pa, (size_t)sz);
	if (v == MAP_FAILED) {
		log_warn(&mlog, "cannot map %s (%llx+%llx); not granted by "
		    "RS?\n", what, (unsigned long long)pa,
		    (unsigned long long)sz);
		return NULL;
	}
	return v;
}

/* Touch every page: the kernel finds a caller's memory by its page table,
 * and a mapping of physical memory is filled in on first touch. */
static void
touch(volatile uint8_t *p, size_t sz)
{
	size_t o;

	for (o = 0; o < sz; o += 4096)
		(void)*(volatile uint32_t *)(volatile void *)(p + o);
}

static void
kick(void)
{
	wr(mss.mbox, 8, 1u << mss.mbox_bit);
}

static void
delay_us(unsigned us)
{
	micro_delay(us);
}

/*===========================================================================*
 *				mss_probe				     *
 *===========================================================================*/
/*
 * Map what is not a device register (SMEM, its lock, the TCSR, the
 * mailbox), read SMEM, and say what is there - nothing written, no block
 * of the modem's touched: its registers are read only once its clocks run.
 */
int
mss_probe(void)
{
	struct smem *s = &mss.smem;
	uint32_t reg;
	const char *name;
	size_t sz;
	unsigned i;
	int r;

	if ((mss.smem_base = map(mss.smem_pa, mss.smem_sz, "SMEM")) == NULL ||
	    (mss.lock_base = map(mss.lock_pa, mss.lock_sz, "TCSR mutex")) ==
	    NULL ||
	    (mss.halt = map(mss.halt_pa, mss.halt_sz, "TCSR")) == NULL ||
	    (mss.mbox = map(mss.mbox_pa, mss.mbox_sz, "mailbox")) == NULL)
		return EPERM;

	memset(s, 0, sizeof(*s));
	s->base = mss.smem_base;
	s->size = (size_t)mss.smem_sz;
	s->lock = (volatile uint32_t *)(mss.lock_base + mss.lock_id * 0x1000);
	s->delay_us = delay_us;
	if (smem_init(s) != 0) {
		log_warn(&mlog, "SMEM: %s\n", s->why);
		return EIO;
	}
	log_info(&mlog, "SMEM version %u, %u items, global partition %zx+%zx, "
	    "modem's %zx+%zx; lock %u reads %x\n", s->version, s->item_count,
	    s->global.off, s->global.size, s->part[1].off, s->part[1].size,
	    mss.lock_id, *s->lock);
	sz = 0;
	log_info(&mlog, "smp2p items: ours (%u) %s, the modem's (%u) %s\n",
	    mss.smp2p_out, smem_get(s, 1, mss.smp2p_out, NULL) ? "there" :
	    "not yet", mss.smp2p_in, smem_get(s, 1, mss.smp2p_in, &sz) ?
	    "there" : "not yet");

	for (i = 0; i < mss.nclk; i++) {
		if (mss.clk[i].ph != qsoc_gcc_phandle())
			continue;
		r = qsoc_clk_on(mss.clk[i].id, &reg, &name);
		log_info(&mlog, "clock %s: %s (%s %08x)\n", mss.clk[i].name,
		    r == 1 ? "on" : r == 0 ? "off" : "not in the table",
		    r >= 0 ? name : "-", r >= 0 ? reg : 0);
	}
	r = qsoc_bcr_asserted(mss.reset_id, &reg, &name);
	log_info(&mlog, "reset %s: %s (%08x)\n", r >= 0 ? name : "?",
	    r == 1 ? "held" : r == 0 ? "released" : "unknown", reg);
	log_info(&mlog, "AXI ports idle: q6 %u modem %u nc %u\n",
	    rd(mss.halt, mss.halt_q6 + AXI_IDLE),
	    rd(mss.halt, mss.halt_modem + AXI_IDLE),
	    rd(mss.halt, mss.halt_nc + AXI_IDLE));
	return OK;
}

/*===========================================================================*
 *				load_mpss				     *
 *===========================================================================*/
static int
load_mpss(void)
{
	struct mdt m;
	const char *why;
	void *file = NULL, *seg = NULL;
	uint8_t *meta = NULL;
	phys_bytes meta_pa;
	size_t len, n, meta_len = 0, meta_alloc = 0, off, total = 0;
	uint32_t meta_perm = HLOS, reloc, boot;
	unsigned k, i;
	char name[48];
	int32_t st;
	int r;

	if ((r = fw_read(mss.fw_mdt, &file, &len, META_MAX)) != OK) {
		log_warn(&mlog, "%s/%s: %d\n", mss_fwdir, mss.fw_mdt, r);
		return r;
	}
	if (mdt_parse(file, len, &m, &why) != 0) {
		log_warn(&mlog, "%s: %s\n", mss.fw_mdt, why);
		r = EINVAL;
		goto out;
	}
	reloc = m.relocatable ? m.min_addr : (uint32_t)mss.mpss_pa;
	boot = m.relocatable ? (uint32_t)mss.mpss_pa : m.min_addr;
	log_info(&mlog, "%s: %u headers, hash %u bytes, %u to load%s, image "
	    "%x..%x\n", mss.fw_mdt, m.nph, (unsigned)m.hash_size, m.nload,
	    m.relocatable ? " (relocatable)" : "", (unsigned)m.min_addr,
	    (unsigned)m.max_addr);
	if (!m.hash_in_file) {
		log_warn(&mlog, "the hash segment is not in the .mdt; not "
		    "written for that\n");
		r = EINVAL;
		goto out;
	}

	/* RMB's validator from zero (q6v5_mpss_load()). */
	wr(mss.rmb, RMB_PMI_CODE_LENGTH, 0);

	/* 3. The metadata. */
	meta_len = m.ehdr_size + m.hash_size;
	meta_alloc = (meta_len + 4095) & ~(size_t)4095;
	if ((meta = alloc_contig(meta_alloc, AC_ALIGN4K, &meta_pa)) == NULL) {
		r = ENOMEM;
		goto out;
	}
	memset(meta, 0, meta_alloc);
	memcpy(meta, file, m.ehdr_size);
	memcpy(meta + m.ehdr_size, (uint8_t *)file + m.hash_off, m.hash_size);
	if ((r = xfer(&meta_perm, 0, 1, meta, meta_alloc, "metadata")) != OK)
		goto out;
	wr(mss.rmb, RMB_PMI_META_DATA, (uint32_t)meta_pa);
	wr(mss.rmb, RMB_MBA_COMMAND, RMB_CMD_META_DATA_READY);
	r = mba_wait(RMB_MBA_META_DATA_AUTH_SUCCESS, 1000, &st);
	log_info(&mlog, "metadata (%zu bytes at %lx): MBA status %d%s\n",
	    meta_len, (unsigned long)meta_pa, (int)st, r == OK ? ", accepted" :
	    r == ETIMEDOUT ? ", no answer in 1 s" : ", REFUSED");
	if (xfer(&meta_perm, 1, 0, meta, meta_alloc, "metadata back") != OK) {
		/* Ours no more, and it may not be freed: leak it. */
		meta = NULL;
	}
	if (r != OK)
		goto out;

	/* 4. The image. */
	(void)xfer(&mss.mpss_perm, 1, 0, mss.mpss, (size_t)mss.mpss_sz,
	    "mpss to us");
	if ((r = xfer(&mss.mpss_perm, 1, 1, mss.mpss, (size_t)mss.mpss_sz,
	    "mpss shared")) != OK)
		goto out;
	for (k = 0; k < m.nload; k++) {
		i = m.load[k];
		off = m.ph[i].paddr - reloc;
		if (m.ph[i].paddr < reloc || off + m.ph[i].memsz > mss.mpss_sz) {
			log_warn(&mlog, "segment %u (%x+%x) outside the region\n",
			    i, (unsigned)m.ph[i].paddr, (unsigned)m.ph[i].memsz);
			r = EINVAL;
			goto out;
		}
		if (m.ph[i].filesz != 0) {
			if (!m.split &&
			    (uint64_t)m.ph[i].offset + m.ph[i].filesz <= len)
				dev_put(mss.mpss + off, (uint8_t *)file +
				    m.ph[i].offset, m.ph[i].filesz);
			else {
				if (mdt_seg_name(mss.fw_mdt, i, name,
				    sizeof(name)) != 0) {
					r = ENAMETOOLONG;
					goto out;
				}
				if ((r = fw_read(name, &seg, &n, SEG_MAX)) !=
				    OK || n != m.ph[i].filesz) {
					log_warn(&mlog, "%s: %d (%zu bytes, the "
					    "segment says %u)\n", name, r, n,
					    (unsigned)m.ph[i].filesz);
					r = r != OK ? r : EINVAL;
					goto out;
				}
				dev_put(mss.mpss + off, seg, n);
				free(seg);
				seg = NULL;
			}
		}
		if (m.ph[i].memsz > m.ph[i].filesz)
			dev_put(mss.mpss + off + m.ph[i].filesz, NULL,
			    m.ph[i].memsz - m.ph[i].filesz);
		total += m.ph[i].memsz;
		__sync_synchronize();
		if (rd(mss.rmb, RMB_PMI_CODE_LENGTH) == 0) {
			wr(mss.rmb, RMB_PMI_CODE_START, boot);
			wr(mss.rmb, RMB_MBA_COMMAND, RMB_CMD_LOAD_READY);
		}
		wr(mss.rmb, RMB_PMI_CODE_LENGTH, (uint32_t)total);
		st = (int32_t)rd(mss.rmb, RMB_MBA_STATUS);
		log_debug(&mlog, "segment %u: +%zx, %u + %u zero; MBA %d\n", i,
		    off, (unsigned)m.ph[i].filesz,
		    (unsigned)(m.ph[i].memsz - m.ph[i].filesz), (int)st);
		if (st < 0) {
			log_warn(&mlog, "MBA refused segment %u: %d\n", i,
			    (int)st);
			r = EIO;
			goto out;
		}
	}
	log_info(&mlog, "%u segments, %zu bytes, in the image region\n",
	    m.nload, total);

	if ((r = xfer(&mss.mpss_perm, 0, 1, mss.mpss, (size_t)mss.mpss_sz,
	    "mpss to the modem")) != OK)
		goto out;
	r = mba_wait(RMB_MBA_AUTH_COMPLETE, 10000, &st);
	log_info(&mlog, "image: MBA status %d%s\n", (int)st, r == OK ?
	    ", authenticated" : r == ETIMEDOUT ? ", no answer in 10 s" :
	    ", REFUSED");
out:
	if (meta != NULL)
		free_contig(meta, meta_alloc);
	free(seg);
	free(file);
	return r;
}

/*===========================================================================*
 *				mss_boot				     *
 *===========================================================================*/
int
mss_boot(int stop_after_mba)
{
	static const char *const out_names[1] = { "master-kernel" };
	void *fw = NULL;
	size_t len;
	uint32_t v;
	int32_t st;
	unsigned t;
	int r;

	/* Files first: a missing one stops before anything is touched. */
	if ((r = fw_read(mss.fw_mba, &fw, &len, 1024 * 1024)) != OK) {
		log_warn(&mlog, "%s/%s: %d; not starting the modem\n",
		    mss_fwdir, mss.fw_mba, r);
		return r;
	}
	if (len > mss.mba_sz) {
		log_warn(&mlog, "%s is larger than its region\n", mss.fw_mba);
		free(fw);
		return EINVAL;
	}

	/* The smp2p edge, before the modem starts and looks for it. */
	memset(&mss.smp2p, 0, sizeof(mss.smp2p));
	mss.smp2p.smem = &mss.smem;
	mss.smp2p.local_pid = mss.smp2p_local;
	mss.smp2p.remote_pid = mss.smp2p_remote;
	mss.smp2p.out_item = mss.smp2p_out;
	mss.smp2p.in_item = mss.smp2p_in;
	mss.smp2p.kick = kick;
	if (smp2p_init(&mss.smp2p, out_names, 1) != 0) {
		log_warn(&mlog, "smp2p: %s\n", mss.smem.why);
		free(fw);
		return EIO;
	}

	/* The regions, mapped and touched while they are ours. */
	if ((mss.mba = map(mss.mba_pa, mss.mba_sz, "the MBA's region")) ==
	    NULL || (mss.mpss = map(mss.mpss_pa, mss.mpss_sz,
	    "the image region")) == NULL ||
	    (mss.qdsp6 = map(mss.qdsp6_pa, mss.qdsp6_sz, "QDSP6SS")) == NULL ||
	    (mss.rmb = map(mss.rmb_pa, mss.rmb_sz, "RMB")) == NULL) {
		free(fw);
		return EPERM;
	}
	touch(mss.mba, (size_t)mss.mba_sz);
	touch(mss.mpss, (size_t)mss.mpss_sz);
	mss.mba_perm = mss.mpss_perm = HLOS;
	dev_put(mss.mba, fw, len);
	free(fw);
	log_info(&mlog, "%s: %zu bytes in the MBA's region\n", mss.fw_mba, len);

	/* 1. Votes, clocks, the reset. */
	if ((r = proxy_votes(1)) != OK)
		log_warn(&mlog, "going on without the proxy votes (%d)\n", r);
	proxy_held = 1;
	if ((r = qsoc_bcr_set(mss.reset_id, 0)) != OK) {
		log_warn(&mlog, "MSS_RESTART: %d\n", r);
		goto fail_votes;
	}
	if ((r = clocks_on()) != OK)
		goto fail_reset;
	log_info(&mlog, "clocks on, MSS_RESTART released; QDSP6SS reset %08x, "
	    "PBL status %u\n", rd(mss.qdsp6, QDSP6SS_RESET),
	    rd(mss.rmb, RMB_PBL_STATUS));

	/* 2. The MBA. */
	if ((r = xfer(&mss.mpss_perm, 0, 1, mss.mpss, (size_t)mss.mpss_sz,
	    "mpss to the modem")) != OK ||
	    (r = xfer(&mss.mba_perm, 0, 1, mss.mba, (size_t)mss.mba_sz,
	    "mba to the modem")) != OK)
		goto fail_reset;
	wr(mss.rmb, RMB_MBA_IMAGE, (uint32_t)mss.mba_pa);
	if ((r = q6_start()) != OK)
		goto fail_reclaim;
	r = mba_wait(0, 5000, &st);
	if (r != OK || (st != RMB_MBA_XPU_UNLOCKED &&
	    st != RMB_MBA_XPU_UNLOCKED_SCRIBBLED)) {
		log_warn(&mlog, "MBA: status %d%s\n", (int)st,
		    r == ETIMEDOUT ? " (no answer in 5 s)" : "");
		r = r != OK ? r : EIO;
		goto fail_halt;
	}
	log_info(&mlog, "MBA booted (status %d), loading mpss\n", (int)st);
	if (stop_after_mba) {
		log_info(&mlog, "stage=mba: stopping here\n");
		r = OK;
		goto fail_halt;
	}

	/* 3, 4. */
	if ((r = load_mpss()) != OK)
		goto fail_halt;

	/* 5. Ready. */
	for (t = 0; t < 500; t++) {
		if (smp2p_in(&mss.smp2p, "slave-kernel", &v) == 0 &&
		    (v & (SMP2P_READY | SMP2P_FATAL)))
			break;
		msleep(10);
	}
	if (smp2p_in(&mss.smp2p, "slave-kernel", &v) != 0 ||
	    !(v & SMP2P_READY)) {
		log_warn(&mlog, "the modem did not say ready in 5 s (%s, %08x)"
		    "\n", mss.smp2p.in ? "its smp2p item there" :
		    "no smp2p item of its", mss.smp2p.in ? v : 0);
		r = ETIMEDOUT;
		goto fail_halt;
	}
	(void)xfer(&mss.mba_perm, 1, 0, mss.mba, (size_t)mss.mba_sz,
	    "mba back");
	log_info(&mlog, "remote processor is now up (smp2p %08x)\n", v);
	mss_watch();
	return OK;

fail_halt:
	halt_all();
fail_reclaim:
	(void)xfer(&mss.mba_perm, 1, 0, mss.mba, (size_t)mss.mba_sz,
	    "mba back");
fail_reset:
	(void)qsoc_bcr_set(mss.reset_id, 1);
fail_votes:
	(void)proxy_votes(0);
	proxy_held = 0;
	return r;
}

/*===========================================================================*
 *				mss_watch				     *
 *===========================================================================*/
/* Called on every tick of the main loop: handover, and what went wrong. */
void
mss_watch(void)
{
	static uint32_t last = 0;
	uint32_t v;

	if (mss.smp2p.out == 0 ||
	    smp2p_in(&mss.smp2p, "slave-kernel", &v) != 0)
		return;
	if (v == last)
		return;
	if ((v & SMP2P_HANDOVER) && proxy_held) {
		(void)proxy_votes(0);
		proxy_held = 0;
		log_info(&mlog, "handover: proxy votes let go\n");
	}
	if ((v & SMP2P_FATAL) && !(last & SMP2P_FATAL))
		log_warn(&mlog, "the modem says FATAL (smp2p %08x)\n", v);
	log_debug(&mlog, "smp2p %08x -> %08x\n", last, v);
	last = v;
}
