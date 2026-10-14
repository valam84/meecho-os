/*
 * The thread pointer: TPIDR_EL0, which thread-local storage is found
 * through (variant I - the block of the running thread starts 16 bytes
 * past it).  EL0 may write the register itself, so on MEECHO this is no
 * system call: the kernel keeps each process's value across switches
 * (arch_save_tls() in the kernel), and libmthread loads its threads' on
 * every switch between them.  libc's __libc_static_tls_setup() (tls.c)
 * gives the main thread its block through _lwp_setprivate().
 *
 *	void	 _lwp_setprivate(void *);
 *	void	*_lwp_getprivate(void);
 */
#include <machine/asm.h>

ENTRY(_lwp_setprivate)
	msr	tpidr_el0, x0
	ret
END(_lwp_setprivate)

ENTRY(_lwp_getprivate)
	mrs	x0, tpidr_el0
	ret
END(_lwp_getprivate)
