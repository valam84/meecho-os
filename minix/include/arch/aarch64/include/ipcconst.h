#ifndef _AARCH64_IPCCONST_H_
#define _AARCH64_IPCCONST_H_

/*
 * Trap numbers.
 *
 * On earm the trap type travels in r3 beside "svc #0". On AArch64 the svc
 * immediate is sixteen bits wide and arrives in ESR_EL1.ISS, so the trap
 * type is the immediate itself: "svc #KERVEC_INTR". That frees a register
 * and hands the kernel the trap type before it has read a word of the
 * frame.
 */
#define KERVEC_INTR 32	/* syscall trap to kernel */
#define IPCVEC_INTR 33	/* ipc trap to kernel  */

/*
 * Register roles, the same as on earm register for register:
 *
 *	x0	IPC call number (SEND, RECEIVE, ...); the result comes back here
 *	x1	source/destination endpoint; the IPC status comes back here
 *	x2	message pointer
 *
 * A KERVEC_INTR trap carries the message pointer in x0. The kernel changes
 * only x0 and x1 on the way back: everything else is restored from the
 * frame it saved on entry.
 */
#define IPC_STATUS_REG		x1

#endif  /* _AARCH64_IPCCONST_H_ */
