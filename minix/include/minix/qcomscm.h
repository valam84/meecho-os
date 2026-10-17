#ifndef _MINIX_QCOMSCM_H
#define _MINIX_QCOMSCM_H

/*
 * The operations of sys_qcomscm(2): calls into the secure monitor (SCM,
 * TrustZone) of a Qualcomm SoC, for the drivers that need one - the Adreno
 * GPU's, whose way out of secure mode is a shader TrustZone loads and
 * checks (port/clover/hw-gpu.md, §5).
 *
 * A user process cannot issue an SMC, and the kernel does not pass one on
 * for it: several of these calls take physical addresses, and TrustZone
 * would lock whatever memory they named.  So the kernel offers operations,
 * not the instruction - each one a call it knows the meaning of and checks
 * the arguments of.  The ones here only ask; none hands TrustZone memory.
 *
 * Every operation answers ENODEV on a machine whose device tree has no
 * "qcom,scm" node (no SMC is issued there at all: on QEMU's virt it would
 * be an undefined instruction), and ENOTSUP when the firmware speaks
 * neither 64- nor 32-bit SMCCC, the conventions this kernel knows.
 *
 *   QCOMSCM_AVAIL		is (svc, cmd) a call the firmware has?
 *				SCM INFO/IS_CALL_AVAIL.  res[0] != 0: yes
 *
 *   QCOMSCM_PAS_SUPPORTED	does the peripheral authentication service
 *				know peripheral "cmd" (13 is the GPU's zap
 *				shader)?  SCM PIL/PAS_IS_SUPPORTED.
 *				res[0] != 0: yes
 *
 * On OK, the answer is what the firmware said: status is its x0 (0 for
 * success, negative SCM error codes otherwise), res its x1..x3, and conv
 * the convention found (QCOMSCM_CONV_*).  EAGAIN: the firmware kept
 * answering "interrupted" with the caller's interrupts held off - ask
 * again.
 */

#define QCOMSCM_AVAIL		1
#define QCOMSCM_PAS_SUPPORTED	2

#define QCOMSCM_CONV_ARM64	64
#define QCOMSCM_CONV_ARM32	32

/* SCM services and commands, as the firmware numbers them. */
#define QCOMSCM_SVC_PIL		0x02
#define QCOMSCM_PIL_PAS_INIT_IMAGE	0x01
#define QCOMSCM_PIL_PAS_MEM_SETUP	0x02
#define QCOMSCM_PIL_PAS_AUTH_AND_RESET	0x05
#define QCOMSCM_PIL_PAS_IS_SUPPORTED	0x07
#define QCOMSCM_SVC_INFO	0x06
#define QCOMSCM_INFO_IS_CALL_AVAIL	0x01

/* Peripheral authentication service ids. */
#define QCOMSCM_PAS_GPU		13

struct qcomscm_answer {
	long status;		/* the firmware's x0 */
	unsigned long res[3];	/* x1..x3 */
	int conv;		/* QCOMSCM_CONV_* */
};

#endif /* _MINIX_QCOMSCM_H */
