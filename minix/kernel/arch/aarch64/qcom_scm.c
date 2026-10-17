/*
 * Qualcomm's secure monitor (SCM): calls into TrustZone, for sys_qcomscm(2).
 *
 * What a call looks like is the SMC calling convention (SMCCC) with
 * Qualcomm's arguments on top (port/clover/hw-gpu.md, §5, from Linux's
 * drivers/firmware/qcom/qcom_scm-smc.c): x0 the function id - fast or
 * yielding, SMC64 or SMC32, owner SIP, (service << 8) | command - x1 the
 * argument description (count in bits 3:0, two bits per argument from bit
 * 4: 0 a value, 2 a buffer the firmware reads and writes), x2..x5 the
 * arguments; the answer is x0 (0 or a negative SCM error) and x1..x3.
 *
 * Which convention the firmware speaks is asked once, the way Linux asks:
 * IS_CALL_AVAIL about itself as a fast SMC64 call, then as SMC32.  A
 * firmware that answers neither speaks the legacy convention, which this
 * does not implement: the operations answer ENOTSUP there.
 *
 * Nothing is issued on a machine whose tree has no "qcom,scm" node.  The
 * instruction itself is the risk: an SMC that nothing at EL3 catches is an
 * undefined instruction in the kernel (QEMU's virt has no EL3 firmware).
 *
 * The kernel offers operations, not the instruction (see <minix/qcomscm.h>
 * for why and what each one is).  Three things here are the reason the
 * operations are the kernel's:
 *
 *   Memory.  PAS_INIT_IMAGE and PAS_MEM_SETUP hand TrustZone physical
 *   addresses, and TrustZone fences what they name - an access from this
 *   side afterwards is an external abort.  So a caller names memory by an
 *   address in its own space, and the kernel translates it page by page:
 *   the metadata has to be one run of ordinary writable RAM of the caller's
 *   (cleaned to memory here, as Linux's buffer is uncached), and the image's
 *   place has to lie wholly inside a "no-map" range of /reserved-memory -
 *   memory nobody else is given, which the caller could only have mapped
 *   because RS granted it that range (the GPU's zap-shader region).
 *
 *   Order.  The kernel remembers what it did with each peripheral since boot
 *   (QCOMSCM_PAS_*): MEM_SETUP and AUTH only after INIT, no second INIT of
 *   an authenticated image, SET_REMOTE_STATE only for one it authenticated.
 *   A driver that starts again asks (QCOMSCM_PAS_STATE) before it touches
 *   memory TrustZone may own.
 *
 *   Interrupts.  The PAS calls are yielding, and TrustZone stops one for a
 *   normal-world interrupt with x0 = 1.  Linux takes the interrupt and
 *   resumes; this kernel holds interrupts off for the whole kernel call, so
 *   resuming at once would find the interrupt still pending and stop again.
 *   The call is held instead (one at a time, the caller's, on the processor
 *   it started on), the caller is told QCOMSCM_INTERRUPTED, takes the
 *   interrupt in user mode, and resumes with QCOMSCM_RESUME - x0 = 1, the
 *   same x1..x5 and the x6 the firmware handed back.  sys_qcomscm() does
 *   that loop; a caller never sees it.
 */

#include "kernel/kernel.h"
#include "kernel/proc.h"

#include <string.h>

#include <machine/vm.h>
#include <minix/cachectl.h>
#include <minix/qcomscm.h>
#include <minix/fdt.h>

#include "arch_proto.h"
#include "kernel/proto.h"

#define SCM_UNKNOWN	0
#define SCM_NONE	1	/* no qcom,scm node: never issue an SMC */
#define SCM_LEGACY	2	/* a firmware this does not speak */
#define SCM_ARM32	3
#define SCM_ARM64	4

static int scm_state = SCM_UNKNOWN;

/* Bits of an SMCCC function id. */
#define SMCCC_FAST		(1UL << 31)
#define SMCCC_SMC64		(1UL << 30)
#define SMCCC_OWNER_SIP		(2UL << 24)

#define SCM_FNID(svc, cmd)	((((svc) & 0xffUL) << 8) | ((cmd) & 0xffUL))

/* Argument descriptions, Linux's QCOM_SCM_ARGS(): a count, and argument i
 * a buffer the firmware reads and writes. */
#define SCM_ARGS(n)		((unsigned long)(n) & 0xf)
#define SCM_ARG_RW(i)		(2UL << (4 + 2 * (i)))

/* What this kernel did with each peripheral since boot (QCOMSCM_PAS_*). */
static unsigned char pas[QCOMSCM_PAS_IDS];

/* The one yielding call TrustZone stopped, until it is resumed. */
static struct {
	int active;
	endpoint_t who;		/* the caller it was made for */
	unsigned cpu;		/* where it stopped: resumed only there */
	int op;			/* QCOMSCM_*, for the bookkeeping at its end */
	unsigned id;		/* the peripheral */
	unsigned long arginfo, a[3];
	unsigned long x6;	/* the firmware's, for the resume */
} held;

/*===========================================================================*
 *				find_scm				     *
 *===========================================================================*/
static int
find_scm(void *cookie, int UNUSED(depth), const char *UNUSED(name),
	const struct fdt_node *node)
{
	if (!fdt_node_is_compatible(node, "qcom,scm"))
		return 0;
	*(int *)cookie = 1;
	return 1;
}

/*===========================================================================*
 *				smc					     *
 *===========================================================================*/
/*
 * One SMC, nothing more: out[] is x0..x3 as the firmware left them, *x6_out
 * its x6 (the resume cookie of an interrupted call).  The barrier first:
 * what the caller wrote for TrustZone to read - an image through a Device
 * mapping, from this same processor - has to have arrived before it looks.
 */
static void
smc(unsigned long fn, unsigned long arginfo, const unsigned long a[3],
	unsigned long x6, unsigned long out[4], unsigned long *x6_out)
{
	register unsigned long r0 __asm__("x0") = fn;
	register unsigned long r1 __asm__("x1") = arginfo;
	register unsigned long r2 __asm__("x2") = a[0];
	register unsigned long r3 __asm__("x3") = a[1];
	register unsigned long r4 __asm__("x4") = a[2];
	register unsigned long r5 __asm__("x5") = 0;
	register unsigned long r6 __asm__("x6") = x6;
	register unsigned long r7 __asm__("x7") = 0;

	/*
	 * SMCCC 1.0 lets the callee change x4..x17; Qualcomm's firmware hands
	 * back the resume cookie in x6.  All of them are outputs or clobbers,
	 * so nothing the compiler keeps there survives by accident.
	 */
	__asm__ volatile("dsb sy\n\tsmc #0"
	    : "+r"(r0), "+r"(r1), "+r"(r2), "+r"(r3), "+r"(r4),
	      "+r"(r5), "+r"(r6), "+r"(r7)
	    :
	    : "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15",
	      "x16", "x17", "memory");

	out[0] = r0;
	out[1] = r1;
	out[2] = r2;
	out[3] = r3;
	*x6_out = r6;
}

/*===========================================================================*
 *				scm_probe				     *
 *===========================================================================*/
/*
 * Find the node and the convention, once.  IS_CALL_AVAIL is asked about
 * itself as a fast call - Linux's __get_convention() - and "yes" is a 0 in
 * x0 with a 1 in x1.  A fast call is atomic: it is never interrupted.
 */
static int
scm_probe(void)
{
	unsigned long a[3] = { 0, 0, 0 }, out[4], x6, self;
	const void *dtb;
	int found = 0;

	if (scm_state != SCM_UNKNOWN)
		return scm_state;
	scm_state = SCM_NONE;

	if (boot_dtb == 0)
		return scm_state;
	dtb = (const void *)phys2vir(boot_dtb);
	if (!fdt_valid(dtb))
		return scm_state;
	(void)fdt_walk(dtb, find_scm, &found);
	if (!found)
		return scm_state;

	self = SCM_FNID(QCOMSCM_SVC_INFO, QCOMSCM_INFO_IS_CALL_AVAIL) |
	    SMCCC_OWNER_SIP;
	a[0] = self;
	smc(SMCCC_FAST | SMCCC_SMC64 | self, 1, a, 0, out, &x6);
	if (out[0] == 0 && out[1] == 1) {
		scm_state = SCM_ARM64;
		return scm_state;
	}
	smc(SMCCC_FAST | self, 1, a, 0, out, &x6);
	scm_state = out[0] == 0 && out[1] == 1 ? SCM_ARM32 : SCM_LEGACY;
	return scm_state;
}

/*===========================================================================*
 *				find_nomap				     *
 *===========================================================================*/
struct nomap_find {
	phys_bytes lo, hi;		/* the range asked about */
	int in_reserved, found;
};

/* Is [lo, hi) wholly inside one "no-map" child of /reserved-memory? */
static int
find_nomap(void *cookie, int depth, const char *name,
	const struct fdt_node *node)
{
	struct nomap_find *f = cookie;
	u64_t addr, size;
	unsigned i, len;

	if (depth == 1) {
		f->in_reserved = !strcmp(name, "reserved-memory");
		return 0;
	}
	if (depth != 2 || !f->in_reserved ||
	    fdt_getprop(node, "no-map", &len) == NULL)
		return 0;
	for (i = 0; fdt_node_reg(node, i, &addr, &size) == 0; i++)
		if (size != 0 && f->lo >= addr && f->hi <= addr + size) {
			f->found = 1;
			return 1;
		}
	return 0;
}

/*===========================================================================*
 *				caller_range				     *
 *===========================================================================*/
/*
 * The physical run behind [addr, addr + len) of the caller's space: the
 * start must be page-aligned (TrustZone wants 4 KiB-aligned buffers) and
 * every page must follow the one before it.  ram: the metadata's buffer -
 * ordinary, cacheable, writable RAM in the linear map; otherwise the image's
 * place - memory outside the linear map, which is what "no-map" makes it.
 */
static int
caller_range(struct proc *caller, vir_bytes addr, vir_bytes len, int ram,
	phys_bytes *pa)
{
	vir_bytes off;
	phys_bytes p;
	u64_t desc;

	if (len == 0 || (addr & AARCH64_PAGE_MASK) != 0 || addr + len < addr)
		return EINVAL;
	for (off = 0; off < len; off += AARCH64_PAGE_SIZE) {
		if (vm_lookup_desc(caller, addr + off, &p, &desc) != OK)
			return EFAULT;
		if (off == 0)
			*pa = p;
		else if (p != *pa + off)
			return EINVAL;		/* not one run */
		if (ram) {
			if ((desc & AARCH64_VM_ATTRINDX(7)) !=
			    AARCH64_VM_ATTRINDX(AARCH64_MAIR_NORMAL) ||
			    (desc & AARCH64_VM_AP_RO) ||
			    !pg_is_ram(p) || !pg_is_ram(p + AARCH64_PAGE_SIZE - 1))
				return EINVAL;
		} else if (pg_is_ram(p) || pg_is_ram(p + AARCH64_PAGE_SIZE - 1))
			return EINVAL;
	}
	/* The SMC32 convention carries 32-bit arguments. */
	if (scm_state == SCM_ARM32 && (u64_t)*pa + len > 0x100000000ULL)
		return EINVAL;
	return OK;
}

/*===========================================================================*
 *				settle					     *
 *===========================================================================*/
/*
 * The firmware's answer to the held call: stopped again (held on), or done
 * - and then what it means for the peripheral.  A PAS call succeeded when
 * x0 and x1 are both 0: x1 is TrustZone's own verdict (Linux's
 * "ret ? : res.result[0]").
 */
static int
settle(struct proc *caller, message *m, const unsigned long out[4],
	unsigned long x6)
{
	int ok;

	m->QSCM_CONV = scm_state == SCM_ARM64 ? QCOMSCM_CONV_ARM64 :
	    QCOMSCM_CONV_ARM32;
	if (out[0] == QCOMSCM_INTERRUPTED) {
		held.active = 1;
		held.who = caller->p_endpoint;
		held.cpu = cpuid;
		held.x6 = x6;
		m->QSCM_STATUS = QCOMSCM_INTERRUPTED;
		m->QSCM_RES[0] = m->QSCM_RES[1] = m->QSCM_RES[2] = 0;
		return OK;
	}

	held.active = 0;
	ok = out[0] == 0 && out[1] == 0;
	switch (held.op) {
	case QCOMSCM_PAS_INIT:
		if (ok && pas[held.id] < QCOMSCM_PAS_INITED)
			pas[held.id] = QCOMSCM_PAS_INITED;
		break;
	case QCOMSCM_PAS_AUTH:
		if (ok)
			pas[held.id] = QCOMSCM_PAS_AUTHED;
		break;
	}
	m->QSCM_STATUS = (long)out[0];
	m->QSCM_RES[0] = out[1];
	m->QSCM_RES[1] = out[2];
	m->QSCM_RES[2] = out[3];
	return OK;
}

/*===========================================================================*
 *				yielding				     *
 *===========================================================================*/
/* A yielding call, as Linux's qcom_scm_call() makes them. */
static int
yielding(struct proc *caller, message *m, int op, unsigned id, unsigned svc,
	unsigned cmd, unsigned long arginfo, unsigned long a0, unsigned long a1,
	unsigned long a2)
{
	unsigned long fn, out[4], x6;

	fn = SCM_FNID(svc, cmd) | SMCCC_OWNER_SIP;
	if (scm_state == SCM_ARM64)
		fn |= SMCCC_SMC64;
	held.op = op;
	held.id = id;
	held.arginfo = arginfo;
	held.a[0] = a0;
	held.a[1] = a1;
	held.a[2] = a2;
	smc(fn, arginfo, held.a, 0, out, &x6);
	return settle(caller, m, out, x6);
}

/*===========================================================================*
 *				arch_qcomscm				     *
 *===========================================================================*/
int
arch_qcomscm(struct proc *caller, message *m)
{
	unsigned long out[4], x6;
	unsigned svc = m->QSCM_SVC, id = m->QSCM_CMD;
	vir_bytes addr = m->QSCM_ADDR, len = m->QSCM_LEN;
	phys_bytes pa;
	int op = m->QSCM_OP, r, n;

	switch (scm_probe()) {
	case SCM_NONE:		return ENODEV;
	case SCM_LEGACY:	return ENOTSUP;
	}

	if (op == QCOMSCM_RESUME) {
		if (!held.active)
			return EINVAL;
		/* Someone else's, still alive: theirs to resume.  A dead
		 * caller's is finished by whoever asks next - TrustZone does
		 * not forget a call. */
		if (held.who != caller->p_endpoint && isokendpt(held.who, &n))
			return EBUSY;
		if (held.cpu != cpuid)
			return EXDEV;
		smc(QCOMSCM_INTERRUPTED, held.arginfo, held.a, held.x6, out,
		    &x6);
		return settle(caller, m, out, x6);
	}

	if (op == QCOMSCM_PAS_STATE) {
		if (id >= QCOMSCM_PAS_IDS)
			return EINVAL;
		m->QSCM_CONV = scm_state == SCM_ARM64 ? QCOMSCM_CONV_ARM64 :
		    QCOMSCM_CONV_ARM32;
		m->QSCM_STATUS = 0;
		m->QSCM_RES[0] = pas[id];
		m->QSCM_RES[1] = m->QSCM_RES[2] = 0;
		return OK;
	}

	if (held.active)
		return EBUSY;

	switch (op) {
	case QCOMSCM_AVAIL:
		if (svc > 0xff || id > 0xff)
			return EINVAL;
		return yielding(caller, m, op, 0, QCOMSCM_SVC_INFO,
		    QCOMSCM_INFO_IS_CALL_AVAIL, SCM_ARGS(1),
		    SCM_FNID(svc, id) | SMCCC_OWNER_SIP, 0, 0);

	case QCOMSCM_PAS_SUPPORTED:
		return yielding(caller, m, op, id, QCOMSCM_SVC_PIL,
		    QCOMSCM_PIL_PAS_IS_SUPPORTED, SCM_ARGS(1), id, 0, 0);

	case QCOMSCM_PAS_INIT:
		if (id >= QCOMSCM_PAS_IDS || len > QCOMSCM_META_MAX)
			return EINVAL;
		if (pas[id] == QCOMSCM_PAS_AUTHED)
			return EALREADY;
		if ((r = caller_range(caller, addr, len, 1, &pa)) != OK)
			return r;
		/* Linux's buffer is uncached; this one is cleaned and
		 * dropped from the caches, and nothing writes it while the
		 * call lasts - its owner is in it. */
		if ((r = arch_cache_range(caller, addr, len,
		    CACHE_CLEAN_INVALIDATE)) != OK)
			return r;
		return yielding(caller, m, op, id, QCOMSCM_SVC_PIL,
		    QCOMSCM_PIL_PAS_INIT_IMAGE, SCM_ARGS(2) | SCM_ARG_RW(1),
		    id, pa, 0);

	case QCOMSCM_PAS_MEM_SETUP: {
		struct nomap_find f;

		if (id >= QCOMSCM_PAS_IDS)
			return EINVAL;
		if (pas[id] == QCOMSCM_PAS_AUTHED)
			return EALREADY;
		if (pas[id] < QCOMSCM_PAS_INITED)
			return EINVAL;
		if ((r = caller_range(caller, addr, len, 0, &pa)) != OK)
			return r;
		memset(&f, 0, sizeof(f));
		f.lo = pa;
		f.hi = pa + len;
		(void)fdt_walk((const void *)phys2vir(boot_dtb), find_nomap,
		    &f);
		if (!f.found)
			return EINVAL;
		pas[id] = QCOMSCM_PAS_GIVEN;
		return yielding(caller, m, op, id, QCOMSCM_SVC_PIL,
		    QCOMSCM_PIL_PAS_MEM_SETUP, SCM_ARGS(3), id, pa, len);
	}

	case QCOMSCM_PAS_AUTH:
		if (id >= QCOMSCM_PAS_IDS)
			return EINVAL;
		if (pas[id] == QCOMSCM_PAS_AUTHED)
			return EALREADY;
		if (pas[id] < QCOMSCM_PAS_INITED)
			return EINVAL;
		pas[id] = QCOMSCM_PAS_GIVEN;
		return yielding(caller, m, op, id, QCOMSCM_SVC_PIL,
		    QCOMSCM_PIL_PAS_AUTH_AND_RESET, SCM_ARGS(1), id, 0, 0);

	case QCOMSCM_REMOTE_STATE:
		if (svc != QCOMSCM_STATE_RESUME || id >= QCOMSCM_PAS_IDS ||
		    pas[id] != QCOMSCM_PAS_AUTHED)
			return EINVAL;
		return yielding(caller, m, op, id, QCOMSCM_SVC_BOOT,
		    QCOMSCM_BOOT_SET_REMOTE_STATE, SCM_ARGS(2), svc, id, 0);
	}
	return EINVAL;
}
