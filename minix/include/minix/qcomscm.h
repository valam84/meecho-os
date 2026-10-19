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
 * the arguments of.  Where a call names memory, the caller names it by an
 * address in its own space, and the kernel translates and checks it.
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
 *   QCOMSCM_PAS_STATE		what this kernel has done with peripheral
 *				"cmd" since boot: res[0] is QCOMSCM_PAS_*.
 *				Asks the firmware nothing.  A driver that
 *				starts again learns from it whether the
 *				image is loaded already - and whether its
 *				memory may still be touched at all.
 *
 *   QCOMSCM_PAS_INIT		PIL/PAS_INIT_IMAGE: peripheral "cmd", its
 *				metadata (the ELF headers and the hash
 *				segment) at [addr, addr + len) - page-aligned,
 *				physically contiguous, ordinary writable RAM
 *				(alloc_contig()).  The kernel cleans it to
 *				memory first; TrustZone fences it while the
 *				call lasts.  EALREADY once "cmd" is
 *				authenticated: resume it instead.
 *
 *   QCOMSCM_PAS_MEM_SETUP	PIL/PAS_MEM_SETUP: where a relocatable image
 *				of peripheral "cmd" goes - [addr, addr + len)
 *				of the caller's mapping (vm_map_phys()) of a
 *				"no-map" range of /reserved-memory, wholly
 *				inside it; nothing else is accepted.  After
 *				PAS_INIT only.
 *
 *   QCOMSCM_PAS_AUTH		PIL/PAS_AUTH_AND_RESET of peripheral "cmd",
 *				after PAS_INIT: TrustZone checks the image
 *				against its hashes and takes its memory.
 *
 *   QCOMSCM_REMOTE_STATE	BOOT/SET_REMOTE_STATE(svc, cmd): state "svc"
 *				(0, resume, the only one) of peripheral
 *				"cmd" - one this kernel authenticated.  The
 *				GPU's zap shader after every power-up but the
 *				first.
 *
 *   QCOMSCM_ASSIGN_MEM	MP/ASSIGN: who may reach [addr, addr + len)
 *				of the caller's space (page-aligned, whole
 *				pages, one physical run).  svc: the current
 *				owners, a mask of VM ids (1 << QCOMSCM_VMID_*);
 *				cmd: the next ones, up to four bytes from the
 *				lowest, each QCOMSCM_DST(vmid, perm), the
 *				first zero byte ending the list.  The range is
 *				either inside a "no-map" range of
 *				/reserved-memory the caller mapped, or the
 *				caller's own RAM (cleaned to memory first) -
 *				which it must take back (dst HLOS) before it
 *				frees it: an access to memory TrustZone gave
 *				away is an external abort.  The modem's image
 *				and metadata, rmtfs's shared memory, Wi-Fi's
 *				MSA (Linux's qcom_scm_assign_mem()).
 *
 * On OK, the answer is what the firmware said: status is its x0 (0 for
 * success, negative SCM error codes otherwise), res its x1..x3, and conv
 * the convention found (QCOMSCM_CONV_*).  A call succeeded when status is
 * 0 and, for the PAS calls and REMOTE_STATE, res[0] is 0 too - the
 * firmware's own verdict on the image comes in x1.
 *
 * The PAS calls and REMOTE_STATE are yielding: TrustZone may stop one for
 * an interrupt of the normal world and say so (x0 = 1, "interrupted").  The
 * kernel cannot take that interrupt while it holds the call, so it answers
 * status QCOMSCM_INTERRUPTED and keeps the call; back in user mode the
 * interrupt is taken, and QCOMSCM_RESUME goes on where TrustZone stopped -
 * on the same processor.  sys_qcomscm() does this itself; a caller never
 * sees QCOMSCM_INTERRUPTED.  While a call is held, every other operation
 * answers EBUSY.
 */

#define QCOMSCM_AVAIL		1
#define QCOMSCM_PAS_SUPPORTED	2
#define QCOMSCM_PAS_STATE	3
#define QCOMSCM_PAS_INIT	4
#define QCOMSCM_PAS_MEM_SETUP	5
#define QCOMSCM_PAS_AUTH	6
#define QCOMSCM_REMOTE_STATE	7
#define QCOMSCM_RESUME		8	/* sys_qcomscm()'s own */
#define QCOMSCM_ASSIGN_MEM	9

#define QCOMSCM_CONV_ARM64	64
#define QCOMSCM_CONV_ARM32	32

/* The firmware's x0 for a yielding call it stopped (see above). */
#define QCOMSCM_INTERRUPTED	1

/* QCOMSCM_PAS_STATE: how far this kernel took a peripheral since boot. */
#define QCOMSCM_PAS_NONE	0	/* nothing */
#define QCOMSCM_PAS_INITED	1	/* its metadata accepted */
#define QCOMSCM_PAS_GIVEN	2	/* its memory named to TrustZone (MEM_SETUP
					 * or AUTH tried): no longer the caller's
					 * to touch, whatever the answer was */
#define QCOMSCM_PAS_AUTHED	3	/* authenticated: running, resume only */

#define QCOMSCM_PAS_IDS		64	/* peripheral numbers this kernel takes */
#define QCOMSCM_META_MAX	(1024 * 1024)	/* PAS_INIT's buffer at most */

/* SCM services and commands, as the firmware numbers them. */
#define QCOMSCM_SVC_BOOT	0x01
#define QCOMSCM_BOOT_SET_REMOTE_STATE	0x0a
#define QCOMSCM_SVC_PIL		0x02
#define QCOMSCM_PIL_PAS_INIT_IMAGE	0x01
#define QCOMSCM_PIL_PAS_MEM_SETUP	0x02
#define QCOMSCM_PIL_PAS_AUTH_AND_RESET	0x05
#define QCOMSCM_PIL_PAS_IS_SUPPORTED	0x07
#define QCOMSCM_SVC_INFO	0x06
#define QCOMSCM_SVC_MP		0x0c
#define QCOMSCM_MP_ASSIGN		0x16
#define QCOMSCM_INFO_IS_CALL_AVAIL	0x01

/* Peripheral authentication service ids. */
#define QCOMSCM_PAS_GPU		13

/* VM ids and permissions of MP/ASSIGN (Linux's qcom_scm.h). */
#define QCOMSCM_VMID_HLOS	0x3
#define QCOMSCM_VMID_MSS_MSA	0xf
#define QCOMSCM_VMID_WLAN	0x18
#define QCOMSCM_VMID_WLAN_CE	0x19
#define QCOMSCM_PERM_READ	0x4
#define QCOMSCM_PERM_WRITE	0x2
#define QCOMSCM_PERM_EXEC	0x1
#define QCOMSCM_PERM_RW		(QCOMSCM_PERM_READ | QCOMSCM_PERM_WRITE)
#define QCOMSCM_PERM_RWX	(QCOMSCM_PERM_RW | QCOMSCM_PERM_EXEC)
/* One new owner in ASSIGN_MEM's cmd: a VM id below 32, its permissions. */
#define QCOMSCM_DST(vmid, perm)	((((vmid) & 0x1f) << 3) | ((perm) & 0x7))
#define QCOMSCM_DST_VMID(b)	(((b) >> 3) & 0x1f)
#define QCOMSCM_DST_PERM(b)	((b) & 0x7)

/* SET_REMOTE_STATE's states. */
#define QCOMSCM_STATE_RESUME	0

struct qcomscm_answer {
	long status;		/* the firmware's x0 */
	unsigned long res[3];	/* x1..x3 */
	int conv;		/* QCOMSCM_CONV_* */
};

#endif /* _MINIX_QCOMSCM_H */
