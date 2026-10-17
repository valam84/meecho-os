#include "syslib.h"

#include <string.h>

#include <minix/qcomscm.h>

/*
 * Ask a Qualcomm SoC's secure monitor (TrustZone) something, through the
 * kernel: the operations and what they answer are in <minix/qcomscm.h>.
 * ENODEV on a machine without one.
 */
int
sys_qcomscm(int op, uint32_t svc, uint32_t cmd, struct qcomscm_answer *ans)
{
	message m;
	int r;

	memset(&m, 0, sizeof(m));
	m.QSCM_OP = op;
	m.QSCM_SVC = svc;
	m.QSCM_CMD = cmd;

	if ((r = _kernel_call(SYS_QCOMSCM, &m)) != OK)
		return r;

	ans->status = (long)m.QSCM_STATUS;
	ans->res[0] = (unsigned long)m.QSCM_RES[0];
	ans->res[1] = (unsigned long)m.QSCM_RES[1];
	ans->res[2] = (unsigned long)m.QSCM_RES[2];
	ans->conv = m.QSCM_CONV;
	return OK;
}
