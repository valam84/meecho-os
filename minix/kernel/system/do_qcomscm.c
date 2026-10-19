/* The kernel call implemented in this file:
 *   m_type:	SYS_QCOMSCM
 *
 * The parameters for this kernel call are:
 *	QSCM_OP		which operation (QCOMSCM_* in <minix/qcomscm.h>)
 *	QSCM_SVC	the SCM service it asks about (QCOMSCM_AVAIL), or the
 *			state to set (QCOMSCM_REMOTE_STATE)
 *	QSCM_CMD	the SCM command (QCOMSCM_AVAIL), or the peripheral
 *			(the PAS operations, QCOMSCM_REMOTE_STATE)
 *	QSCM_ADDR	memory the call names, in the caller's address space
 *	QSCM_LEN	(QCOMSCM_PAS_INIT, QCOMSCM_PAS_MEM_SETUP, QCOMSCM_ASSIGN_MEM)
 * and the answer:
 *	QSCM_CONV	the calling convention the firmware speaks
 *	QSCM_STATUS	the firmware's x0
 *	QSCM_RES	its x1..x3
 *
 * A call into the secure monitor of a Qualcomm SoC, for a driver whose
 * device TrustZone has a say in - the Adreno GPU leaves secure mode only by
 * a shader TrustZone loads (port/clover/hw-gpu.md, §5).  The kernel does
 * not pass an SMC through: it offers operations it knows the meaning of,
 * and translates and checks every address one names (arch_qcomscm()).
 * Which processes may make the call at all is the ordinary per-call
 * privilege ("system QCOMSCM;").
 */

#include "kernel/system.h"

#include <minix/qcomscm.h>

#if USE_QCOMSCM

/*===========================================================================*
 *				do_qcomscm				     *
 *===========================================================================*/
int
do_qcomscm(struct proc * caller, message * m_ptr)
{
	switch (m_ptr->QSCM_OP) {
	case QCOMSCM_AVAIL:
	case QCOMSCM_PAS_SUPPORTED:
	case QCOMSCM_PAS_STATE:
	case QCOMSCM_PAS_INIT:
	case QCOMSCM_PAS_MEM_SETUP:
	case QCOMSCM_PAS_AUTH:
	case QCOMSCM_REMOTE_STATE:
	case QCOMSCM_RESUME:
	case QCOMSCM_ASSIGN_MEM:
		break;
	default:
		return EINVAL;
	}

	return arch_qcomscm(caller, m_ptr);
}

#endif /* USE_QCOMSCM */
