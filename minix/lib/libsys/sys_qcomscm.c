#include "syslib.h"

#include <string.h>

#include <minix/qcomscm.h>

/*
 * Ask a Qualcomm SoC's secure monitor (TrustZone) something, or have it do
 * something, through the kernel: the operations and what they answer are in
 * <minix/qcomscm.h>.  ENODEV on a machine without one.
 *
 * A yielding call TrustZone stops for an interrupt comes back as
 * QCOMSCM_INTERRUPTED: the kernel holds it, and here - in user mode, where
 * the interrupt that stopped it is taken on the way out of the kernel - it
 * is resumed, for as long as the firmware keeps stopping.  The caller sees
 * only the end.  A resume the kernel refuses ends the loop with its error,
 * and the call stays held (every other operation EBUSY).
 */
static int
call(message *m, struct qcomscm_answer *ans)
{
	int r;

	for (;;) {
		if ((r = _kernel_call(SYS_QCOMSCM, m)) != OK)
			return r;
		if (m->QSCM_STATUS != QCOMSCM_INTERRUPTED)
			break;
		memset(m, 0, sizeof(*m));
		m->QSCM_OP = QCOMSCM_RESUME;
	}

	ans->status = (long)m->QSCM_STATUS;
	ans->res[0] = (unsigned long)m->QSCM_RES[0];
	ans->res[1] = (unsigned long)m->QSCM_RES[1];
	ans->res[2] = (unsigned long)m->QSCM_RES[2];
	ans->conv = m->QSCM_CONV;
	return OK;
}

int
sys_qcomscm(int op, uint32_t svc, uint32_t cmd, struct qcomscm_answer *ans)
{
	message m;

	memset(&m, 0, sizeof(m));
	m.QSCM_OP = op;
	m.QSCM_SVC = svc;
	m.QSCM_CMD = cmd;
	return call(&m, ans);
}

/* The PAS operations that name memory: [addr, addr + len) of the caller's. */
int
sys_qcomscm_mem(int op, uint32_t id, void *addr, size_t len,
	struct qcomscm_answer *ans)
{
	message m;

	memset(&m, 0, sizeof(m));
	m.QSCM_OP = op;
	m.QSCM_CMD = id;
	m.QSCM_ADDR = (vir_bytes)addr;
	m.QSCM_LEN = len;
	return call(&m, ans);
}
