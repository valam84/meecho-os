#include <minix/ipcconst.h>
#include <machine/asm.h>

ENTRY(ipc_minix_kerninfo)
	str	x0, [sp, #-16]!	/* save the out pointer */
	mov	x1, #0
	mov	x2, #0
	mov	x0, #MINIX_KERNINFO	/* _ipc_minix_kerninfo() */
	svc	#IPCVEC_INTR		/* trap to kernel */
	ldr	x2, [sp], #16		/* x2 = return struct ptr (was x0) */
	str	x1, [x2]		/* the kernel answers in the status register */
	ret
END(ipc_minix_kerninfo)
