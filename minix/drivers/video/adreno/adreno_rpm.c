/*
 * VDDMX for the GPU (port/clover/hw-gpu.md, §20): the votes go to the
 * qcom_rpm service (<minix/qcom_rpm.h>), which owns the GLINK channel to
 * the RPM - the same request rpmpd.c makes for the GPU's power domain.
 * Which level, and when around the clock, is adreno_gpu.c's to say; this
 * file only carries a vote.  The host stand answers ad_rpm_set() with a
 * model of the rail instead (port/test/adreno/model.c).
 */

#include "adreno.h"

#include <minix/drivers.h>
#include <minix/ds.h>
#include <minix/qcom_rpm.h>

#include <string.h>

/* One key of VDDMX for the active set: OK, or why not (ENODEV: no
 * qcom_rpm, or it has no RPM link). */
int
ad_rpm_set(uint32_t key, uint32_t value)
{
	static endpoint_t ep = NONE;
	message m;
	int r;

	if (ep == NONE && ds_retrieve_label_endpt("qcom_rpm", &ep) != OK) {
		ep = NONE;
		return ENODEV;
	}
	memset(&m, 0, sizeof(m));
	m.m_type = QRPM_SET;
	m.m_u32.data[QRPM_TYPE] = QRPM_RES_RWMX;
	m.m_u32.data[QRPM_ID] = 0;
	m.m_u32.data[QRPM_KEY] = key;
	m.m_u32.data[QRPM_VALUE] = value;
	if ((r = ipc_sendrec(ep, &m)) != OK) {
		ep = NONE;		/* restarted, perhaps: look again */
		return r;
	}
	return m.m_type;
}
