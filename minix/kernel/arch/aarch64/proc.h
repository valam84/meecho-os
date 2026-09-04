/*
 * Processes.
 *
 * A shadow of the generic kernel's struct proc, which carries scheduling
 * state, IPC state, accounting and a great deal else. What is here is what
 * running one program at EL0 needs: its registers, its address space, and a
 * name to put in a message.
 *
 * The register field is called p_reg, and is a struct stackframe_s, because
 * that is what the generic kernel calls it and what it is - see trap.h.
 */

#ifndef _AARCH64_PROC_H_
#define _AARCH64_PROC_H_

#include <stdint.h>

#include "trap.h"

struct proc {
	const char *p_name;
	struct stackframe_s p_reg;
	uint64_t p_ttbr0;	/* physical address of its root table */
	unsigned p_asid;
};

/*
 * System call numbers, and the register they arrive in.
 *
 * x8 holds the number and x0..x2 the arguments, which is the AArch64 Linux
 * convention rather than a MINIX one. MINIX passes a message, and what that
 * message looks like on LP64 is stage 3's question - it is the one thing in
 * this port that cannot be decided by reading a manual. Until then this is a
 * placeholder chosen for being unsurprising.
 */
#define SYS_WRITE	0
#define SYS_EXIT	1
#define SYS_GETTICKS	2

/* Build the first process and run it. Does not return. */
void proc_start_first(void) __attribute__((noreturn));

/* Called from the trap handler. */
void syscall_handler(struct stackframe_s *frame);
void proc_fault(void) __attribute__((noreturn));

/*
 * Note a tick that interrupted the process rather than the kernel. The clock
 * handler decides which by looking at the frame it interrupted; counting them
 * separately is how this stage shows that a process really is preemptible.
 */
void proc_count_user_tick(void);

/*
 * Copy to and from a process's memory, failing rather than faulting when the
 * address is not the process's to touch. See usercopy.S.
 */
int copyin(uint64_t uaddr, void *dst, uint64_t len);
int copyout(uint64_t uaddr, const void *src, uint64_t len);

#endif /* _AARCH64_PROC_H_ */
