#include <minix/ipcconst.h>
#include <machine/asm.h>

/**========================================================================* */
/*                           IPC assembly routines			  * */
/**========================================================================* */

/*
 * Register roles are those of <machine/ipcconst.h>: x0 carries the call
 * number and returns the result, x1 carries the endpoint and returns the
 * IPC status, x2 carries the message pointer. The trap type is the svc
 * immediate. The kernel restores every other register from the frame, and
 * the stack pointer is kept 16-byte aligned throughout because AArch64
 * faults on a misaligned one.
 */

ENTRY(_ipc_send_intr)
	mov	x2, x1		/* x2 = msg ptr */
	mov	x1, x0		/* x1 = src_dest */
	mov	x0, #SEND	/* _ipc_send(dest, ptr) */
	svc	#IPCVEC_INTR	/* trap to kernel */
	ret
END(_ipc_send_intr)

ENTRY(_ipc_receive_intr)
	str	x2, [sp, #-16]!	/* save status ptr */
	mov	x2, x1		/* x2 = msg ptr */
	mov	x1, x0		/* x1 = src_dest */
	mov	x0, #RECEIVE	/* _ipc_receive(src, ptr) */
	svc	#IPCVEC_INTR	/* trap to kernel */
	ldr	x2, [sp], #16	/* restore status ptr */
	str	w1, [x2]	/* *status_ptr = IPC status (an int) */
	ret
END(_ipc_receive_intr)

ENTRY(_ipc_sendrec_intr)
	mov	x2, x1		/* x2 = msg ptr */
	mov	x1, x0		/* x1 = src_dest */
	mov	x0, #SENDREC	/* _ipc_sendrec(srcdest, ptr) */
	svc	#IPCVEC_INTR	/* trap to kernel */
	ret
END(_ipc_sendrec_intr)

ENTRY(_ipc_notify_intr)
	mov	x1, x0		/* x1 = src_dest */
	mov	x0, #NOTIFY	/* _ipc_notify(srcdst) */
	svc	#IPCVEC_INTR	/* trap to kernel */
	ret
END(_ipc_notify_intr)

ENTRY(_ipc_sendnb_intr)
	mov	x2, x1		/* x2 = msg ptr */
	mov	x1, x0		/* x1 = src_dest */
	mov	x0, #SENDNB	/* _ipc_sendnb(dest, ptr) */
	svc	#IPCVEC_INTR	/* trap to kernel */
	ret
END(_ipc_sendnb_intr)

ENTRY(_ipc_senda_intr)
	mov	x2, x0		/* x2 = table */
	/* x1 already holds count */
	mov	x0, #SENDA	/* _ipc_senda(table, count) */
	svc	#IPCVEC_INTR	/* trap to kernel */
	ret
END(_ipc_senda_intr)
