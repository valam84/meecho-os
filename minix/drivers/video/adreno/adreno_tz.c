/*
 * TrustZone, asked through the kernel (sys_qcomscm(2)): is the way to the
 * zap shader there (port/clover/hw-gpu.md, §5)?  Questions only - no call
 * here hands TrustZone memory.
 *
 * The calls the zap shader needs: PAS_INIT_IMAGE, MEM_SETUP and
 * AUTH_AND_RESET of the peripheral authentication service, and
 * SET_REMOTE_STATE of the boot service on every start after the first;
 * and whether the service knows peripheral 13, the GPU's.
 */

#include "adreno.h"

#include <minix/qcomscm.h>

#define QCOMSCM_SVC_BOOT		0x01
#define QCOMSCM_BOOT_SET_REMOTE_STATE	0x0a

static const struct {
	unsigned svc, cmd;
	const char *name;
} calls[] = {
	{ QCOMSCM_SVC_PIL, QCOMSCM_PIL_PAS_INIT_IMAGE,		"init" },
	{ QCOMSCM_SVC_PIL, QCOMSCM_PIL_PAS_MEM_SETUP,		"mem" },
	{ QCOMSCM_SVC_PIL, QCOMSCM_PIL_PAS_AUTH_AND_RESET,	"auth" },
	{ QCOMSCM_SVC_PIL, QCOMSCM_PIL_PAS_IS_SUPPORTED,	"supp" },
	{ QCOMSCM_SVC_BOOT, QCOMSCM_BOOT_SET_REMOTE_STATE,	"remote" },
};

void
tz_report(void)
{
	struct qcomscm_answer a;
	char line[160], part[32];
	unsigned i;
	int r, conv = 0;

	line[0] = '\0';
	for (i = 0; i < sizeof(calls) / sizeof(calls[0]); i++) {
		if ((r = sys_qcomscm(QCOMSCM_AVAIL, calls[i].svc, calls[i].cmd,
		    &a)) != OK) {
			log_warn(&ad_log, "Q12 tz: sys_qcomscm: %d%s\n", r,
			    r == ENODEV ? " (no qcom,scm in the tree)" :
			    r == ENOTSUP ? " (legacy convention)" : "");
			return;
		}
		conv = a.conv;
		if (a.status != 0)
			snprintf(part, sizeof(part), " %s err%ld", calls[i].name,
			    a.status);
		else
			snprintf(part, sizeof(part), " %s %lu", calls[i].name,
			    a.res[0]);
		strlcat(line, part, sizeof(line));
	}
	r = sys_qcomscm(QCOMSCM_PAS_SUPPORTED, 0, QCOMSCM_PAS_GPU, &a);
	if (r != OK)
		snprintf(part, sizeof(part), "; pas %d: call %d",
		    QCOMSCM_PAS_GPU, r);
	else if (a.status != 0)
		snprintf(part, sizeof(part), "; pas %d: err%ld",
		    QCOMSCM_PAS_GPU, a.status);
	else
		snprintf(part, sizeof(part), "; pas %d: %s", QCOMSCM_PAS_GPU,
		    a.res[0] != 0 ? "supported" : "NOT supported");
	strlcat(line, part, sizeof(line));
	log_info(&ad_log, "Q12 tz smccc%d avail:%s\n", conv, line);
}
