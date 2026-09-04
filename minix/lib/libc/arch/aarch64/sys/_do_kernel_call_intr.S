#include <minix/ipcconst.h>
#include <machine/asm.h>

ENTRY(_do_kernel_call_intr)
	/* x0 already holds the message pointer */
	svc	#KERVEC_INTR	/* the immediate selects the trap type */
	ret
END(_do_kernel_call_intr)
