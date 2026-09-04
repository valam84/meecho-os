#ifndef _IPC_CONST_H
#define _IPC_CONST_H

#include <machine/ipcconst.h>

 /* System call numbers that are passed when trapping to the kernel. */
#define SEND		   1	/* blocking send */
#define RECEIVE		   2	/* blocking receive */
#define SENDREC	 	   3  	/* SEND + RECEIVE */
#define NOTIFY		   4	/* asynchronous notify */
#define SENDNB             5    /* nonblocking send */
#define MINIX_KERNINFO     6    /* request kernel info structure */
#define SENDA		   16	/* asynchronous send */
#define IPCNO_HIGHEST	SENDA
/* The shape of a message.  One size for every architecture: a payload wide
 * enough for all 256 variants under LP64, where pointers, size_t and
 * vir_bytes are eight bytes wide.  A 32-bit build lays the same fields out
 * smaller and pays for the room it does not use; that is deliberate, it is a
 * reference build and not a product.  Why 120 and not 56, 64 or 88, and what
 * the extra bytes cost per IPC crossing: port/PORTING-LOG.md, stage 3.
 */
#define M_PAYLOAD_SIZE	 120	/* bytes of payload a message carries */
#define M_MESSAGE_SIZE	 128	/* whole message: 8-byte header + payload */

/* Alignment of a message.  At 64 a message is exactly two cache lines on a
 * Cortex-A72 and never straddles a third; at the historic 16 a 128-byte
 * message would touch three lines out of four.  This costs only padding in
 * the structures that embed a message.  The cache effect itself cannot be
 * confirmed under QEMU, which models no cache -- confirm on hardware.
 */
#define M_MESSAGE_ALIGN	  64

/* Check that a message payload type fits the payload of a message.  On LP64,
 * the data model the payload was sized for, it must fill it exactly: the
 * padding[] arrays in <minix/ipc.h> are generated for that model by
 * minix/kernel/arch/aarch64/tools/gen-msgpadding.py, and an exact match is
 * what proves the header still matches its generator.  Elsewhere the same
 * fields come out smaller, so there the check is an upper bound only.
 * This is a compile time check. */
#define _ASSERT_MSG_SIZE(msg_type) \
    typedef int _ASSERT_##msg_type[/* CONSTCOND */ \
	(sizeof(msg_type) <= M_PAYLOAD_SIZE && \
	 (sizeof(long) != 8 || sizeof(msg_type) == M_PAYLOAD_SIZE)) ? 1 : -1]

/* Macros for IPC status code manipulation. */
#define IPC_STATUS_CALL_SHIFT	0
#define IPC_STATUS_CALL_MASK	0x3F
#define IPC_STATUS_CALL(status)	\
	(((status) >> IPC_STATUS_CALL_SHIFT) & IPC_STATUS_CALL_MASK)
#define IPC_STATUS_CALL_TO(call) \
	(((call) & IPC_STATUS_CALL_MASK) << IPC_STATUS_CALL_SHIFT)

#define IPC_FLG_MSG_FROM_KERNEL	1 /* this message originated in the kernel on
				     behalf of a process, this is a trusted
				     message, never reply to the sender
				 */
#define IPC_STATUS_FLAGS_SHIFT	16
#define IPC_STATUS_FLAGS(flgs)	((flgs) << IPC_STATUS_FLAGS_SHIFT)
#define IPC_STATUS_FLAGS_TEST(status, flgs)	\
		(((status) >> IPC_STATUS_FLAGS_SHIFT) & (flgs))
#endif /* IPC_CONST_H */
