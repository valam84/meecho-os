/*
 * Qualcomm's secure monitor (SCM): calls into TrustZone, for sys_qcomscm(2).
 *
 * What a call looks like is the SMC calling convention (SMCCC) with
 * Qualcomm's arguments on top (port/clover/hw-gpu.md, §5, from Linux's
 * drivers/firmware/qcom/qcom_scm-smc.c): x0 the function id - fast or
 * yielding, SMC64 or SMC32, owner SIP, (service << 8) | command - x1 the
 * argument description (count in bits 3:0), x2..x5 the arguments; the
 * answer is x0 (0 or a negative SCM error) and x1..x3.  A yielding call may
 * answer 1, "interrupted": it is resumed by calling again with x0 = 1, the
 * same x1..x5 and the x6 the firmware returned.
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
 * for why).  The two here only ask questions and hand TrustZone nothing.
 */

#include "kernel/kernel.h"

#include <string.h>

#include <minix/qcomscm.h>
#include <minix/fdt.h>

#include "arch_proto.h"

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

#define SCM_INTERRUPTED		1
#define SCM_RESUMES		1000	/* "interrupted" answers before EAGAIN */

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
 * One call, resumed while the firmware answers "interrupted".  The kernel
 * holds interrupts off, so a yielding call that keeps being interrupted by
 * an interrupt nobody takes would never finish: after SCM_RESUMES answers
 * it is given up with EAGAIN, and the caller asks again from user mode,
 * where the interrupt gets taken.
 */
static int
smc(unsigned long fn, unsigned long arginfo, unsigned long a0,
	unsigned long a1, long *status, unsigned long res[3])
{
	unsigned long x6 = 0, first = fn;
	unsigned n;

	for (n = 0; n < SCM_RESUMES; n++) {
		register unsigned long r0 __asm__("x0") = first;
		register unsigned long r1 __asm__("x1") = arginfo;
		register unsigned long r2 __asm__("x2") = a0;
		register unsigned long r3 __asm__("x3") = a1;
		register unsigned long r4 __asm__("x4") = 0;
		register unsigned long r5 __asm__("x5") = 0;
		register unsigned long r6 __asm__("x6") = x6;
		register unsigned long r7 __asm__("x7") = 0;

		/*
		 * SMCCC 1.0 lets the callee change x4..x17; Qualcomm's
		 * firmware hands back the resume cookie in x6.  All of them
		 * are outputs or clobbers, so nothing the compiler keeps
		 * there survives by accident.
		 */
		__asm__ volatile("smc #0"
		    : "+r"(r0), "+r"(r1), "+r"(r2), "+r"(r3), "+r"(r4),
		      "+r"(r5), "+r"(r6), "+r"(r7)
		    :
		    : "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15",
		      "x16", "x17", "memory");

		if (r0 != SCM_INTERRUPTED) {
			*status = (long)r0;
			res[0] = r1;
			res[1] = r2;
			res[2] = r3;
			return OK;
		}
		first = SCM_INTERRUPTED;
		x6 = r6;
		/* x1..x5 go out again as the first call had them. */
	}
	return EAGAIN;
}

/*===========================================================================*
 *				scm_probe				     *
 *===========================================================================*/
/*
 * Find the node and the convention, once.  IS_CALL_AVAIL is asked about
 * itself as a fast call - Linux's __get_convention() - and "yes" is a 0 in
 * x0 with a 1 in x1.
 */
static int
scm_probe(void)
{
	unsigned long res[3], self;
	const void *dtb;
	long status;
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
	if (smc(SMCCC_FAST | SMCCC_SMC64 | self, 1, self, 0, &status,
	    res) == OK && status == 0 && res[0] == 1)
		scm_state = SCM_ARM64;
	else if (smc(SMCCC_FAST | self, 1, self, 0, &status, res) == OK &&
	    status == 0 && res[0] == 1)
		scm_state = SCM_ARM32;
	else
		scm_state = SCM_LEGACY;
	return scm_state;
}

/*===========================================================================*
 *				arch_qcomscm				     *
 *===========================================================================*/
int
arch_qcomscm(int op, u32_t svc, u32_t cmd, int *conv, i64_t *status,
	u64_t res[3])
{
	unsigned long fn, arg, r[3];
	long st;
	int s;

	switch (scm_probe()) {
	case SCM_NONE:		return ENODEV;
	case SCM_LEGACY:	return ENOTSUP;
	}

	switch (op) {
	case QCOMSCM_AVAIL:
		if (svc > 0xff || cmd > 0xff)
			return EINVAL;
		fn = SCM_FNID(QCOMSCM_SVC_INFO, QCOMSCM_INFO_IS_CALL_AVAIL);
		arg = SCM_FNID(svc, cmd) | SMCCC_OWNER_SIP;
		break;
	case QCOMSCM_PAS_SUPPORTED:
		fn = SCM_FNID(QCOMSCM_SVC_PIL, QCOMSCM_PIL_PAS_IS_SUPPORTED);
		arg = cmd;
		break;
	default:
		return EINVAL;
	}

	/* Yielding calls, as Linux's qcom_scm_call() makes them. */
	fn |= SMCCC_OWNER_SIP;
	if (scm_state == SCM_ARM64)
		fn |= SMCCC_SMC64;
	if ((s = smc(fn, 1, arg, 0, &st, r)) != OK)
		return s;

	*conv = scm_state == SCM_ARM64 ? QCOMSCM_CONV_ARM64 :
	    QCOMSCM_CONV_ARM32;
	*status = st;
	res[0] = r[0];
	res[1] = r[1];
	res[2] = r[2];
	return OK;
}
