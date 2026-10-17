/* The kernel call implemented in this file:
 *   m_type:	SYS_QCOMSCM
 *
 * The parameters for this kernel call are:
 *	QSCM_OP		which operation (QCOMSCM_* in <minix/qcomscm.h>)
 *	QSCM_SVC	the SCM service it asks about (QCOMSCM_AVAIL)
 *	QSCM_CMD	the SCM command (QCOMSCM_AVAIL), or the peripheral
 *			(QCOMSCM_PAS_SUPPORTED)
 * and the answer:
 *	QSCM_CONV	the calling convention the firmware speaks
 *	QSCM_STATUS	the firmware's x0
 *	QSCM_RES	its x1..x3
 *
 * A call into the secure monitor of a Qualcomm SoC, for a driver whose
 * device TrustZone has a say in - the Adreno GPU leaves secure mode only by
 * a shader TrustZone loads (port/clover/hw-gpu.md, §5).  The kernel does
 * not pass an SMC through: it offers operations it knows the meaning of,
 * and the ones there are so far only ask.  Which processes may make the
 * call at all is the ordinary per-call privilege ("system QCOMSCM;").
 */

#include "kernel/system.h"

#include <minix/qcomscm.h>

#if USE_QCOMSCM

/*===========================================================================*
 *				do_qcomscm				     *
 *===========================================================================*/
int
do_qcomscm(struct proc * UNUSED(caller), message * m_ptr)
{
	u64_t res[3];
	i64_t status;
	int conv, r;

	switch (m_ptr->QSCM_OP) {
	case QCOMSCM_AVAIL:
	case QCOMSCM_PAS_SUPPORTED:
		break;
	default:
		return EINVAL;
	}

	if ((r = arch_qcomscm(m_ptr->QSCM_OP, m_ptr->QSCM_SVC,
	    m_ptr->QSCM_CMD, &conv, &status, res)) != OK)
		return r;

	m_ptr->QSCM_CONV = conv;
	m_ptr->QSCM_STATUS = status;
	m_ptr->QSCM_RES[0] = res[0];
	m_ptr->QSCM_RES[1] = res[1];
	m_ptr->QSCM_RES[2] = res[2];
	return OK;
}

#endif /* USE_QCOMSCM */
